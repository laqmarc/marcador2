#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#include <cstring>

namespace {

constexpr uint8_t SCORE_BUTTON_PIN = 0;
constexpr unsigned long DEBOUNCE_MS = 35;
constexpr unsigned long DOUBLE_CLICK_MS = 320;

constexpr char DEVICE_NAME[] = "MarcadorPadel-BLE";
constexpr char SERVICE_UUID[] = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
constexpr char STATE_UUID[] = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
constexpr char COMMAND_UUID[] = "e3223119-9445-4e96-a4a1-85358c4046a2";
constexpr bool ENABLE_BLE = false;

constexpr uint8_t TFT_BACKLIGHT_PIN = 21;
constexpr uint8_t TOUCH_CS = 33;
constexpr uint8_t TOUCH_IRQ = 36;
constexpr uint8_t TOUCH_MOSI = 32;
constexpr uint8_t TOUCH_MISO = 39;
constexpr uint8_t TOUCH_CLK = 25;

constexpr int TOUCH_X_MIN = 200;
constexpr int TOUCH_X_MAX = 3800;
constexpr int TOUCH_Y_MIN = 200;
constexpr int TOUCH_Y_MAX = 3800;
constexpr uint16_t TOUCH_DEBOUNCE_MS = 220;

constexpr uint16_t SCREEN_WIDTH = 320;
constexpr uint16_t SCREEN_HEIGHT = 240;

constexpr uint16_t COLOR_BG = 0x08A2;
constexpr uint16_t COLOR_PANEL = 0x1924;
constexpr uint16_t COLOR_PANEL_ALT = 0x21C7;
constexpr uint16_t COLOR_LINE = 0x4208;
constexpr uint16_t COLOR_TEXT = TFT_WHITE;
constexpr uint16_t COLOR_MUTED = 0xBDF7;
constexpr uint16_t COLOR_A = 0xFC66;
constexpr uint16_t COLOR_B = 0x055D;
constexpr uint16_t COLOR_TIMER = 0xFD80;
constexpr uint16_t COLOR_SUCCESS = 0x3E6A;
constexpr uint16_t COLOR_WARNING = 0xF2A6;
constexpr uint16_t COLOR_DANGER = 0xE186;

constexpr uint8_t MAX_GAME_LOG = 10;
constexpr uint8_t MAX_SET_HISTORY = 3;

struct Button {
  uint8_t pin;
  bool stableLevel;
  bool lastReading;
  unsigned long lastChangeAt;
};

struct TeamScore {
  int sets;
  int games;
  int points;
};

enum class ScreenMode {
  Home,
  Match,
};

enum class SportId {
  Padel,
  Football,
  Basketball,
  Paret,
};

struct UiRect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

struct MatchSnapshot {
  TeamScore teamA;
  TeamScore teamB;
  bool timerRunning;
  unsigned long elapsedMs;
  uint8_t gameLogCount;
  char gameLog[MAX_GAME_LOG];
  uint8_t setHistoryCount;
  uint8_t setScoresA[MAX_SET_HISTORY];
  uint8_t setScoresB[MAX_SET_HISTORY];
};

constexpr UiRect TIMER_RECT{98, 8, 110, 26};
constexpr UiRect STATUS_RECT{10, 8, 76, 26};
constexpr UiRect STATUS_MSG_RECT{214, 8, 96, 26};
constexpr UiRect CARD_A_RECT{10, 44, 146, 108};
constexpr UiRect CARD_B_RECT{164, 44, 146, 108};
constexpr UiRect LOG_RECT{10, 158, 300, 30};
constexpr UiRect BTN_A_RECT{10, 196, 72, 34};
constexpr UiRect BTN_UNDO_RECT{88, 196, 68, 34};
constexpr UiRect BTN_TIMER_RECT{162, 196, 68, 34};
constexpr UiRect BTN_B_RECT{236, 196, 72, 34};
constexpr UiRect HOME_TITLE_RECT{10, 12, 300, 26};
constexpr UiRect HOME_SUBTITLE_RECT{10, 38, 300, 18};
constexpr UiRect HOME_PADEL_RECT{10, 62, 145, 64};
constexpr UiRect HOME_FOOTBALL_RECT{165, 62, 145, 64};
constexpr UiRect HOME_BASKET_RECT{10, 142, 145, 64};
constexpr UiRect HOME_PARET_RECT{165, 142, 145, 64};

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

Button scoreButton{SCORE_BUTTON_PIN, HIGH, HIGH, 0};
TeamScore teamA{0, 0, 0};
TeamScore teamB{0, 0, 0};
ScreenMode currentScreen = ScreenMode::Home;
SportId currentSport = SportId::Padel;

bool singleClickPending = false;
unsigned long lastButtonReleaseAt = 0;
bool uiDirty = true;
bool stateDirty = true;
bool touchHeld = false;
unsigned long lastTouchAt = 0;
bool timerRunning = false;
unsigned long timerAccumulatedMs = 0;
unsigned long timerStartedAt = 0;
unsigned long lastRenderedSecond = ~0UL;
bool timerDirty = true;
bool hasUndo = false;
MatchSnapshot undoSnapshot{};
char gameLog[MAX_GAME_LOG] = {};
uint8_t gameLogCount = 0;
uint8_t setHistoryCount = 0;
uint8_t setScoresA[MAX_SET_HISTORY] = {};
uint8_t setScoresB[MAX_SET_HISTORY] = {};
char statusLine[24] = "";

BLECharacteristic *stateCharacteristic = nullptr;
bool deviceConnected = false;
bool restartAdvertising = false;

const char *sportName() {
  switch (currentSport) {
    case SportId::Padel:
      return "PADEL";
    case SportId::Football:
      return "FUTBOL";
    case SportId::Basketball:
      return "BASQUET";
    case SportId::Paret:
      return "PARET";
  }
  return "";
}

bool isPadelSport() {
  return currentSport == SportId::Padel;
}

bool isParetSport() {
  return currentSport == SportId::Paret;
}

bool contains(const UiRect &rect, int16_t x, int16_t y) {
  return x >= rect.x && x < rect.x + rect.w && y >= rect.y &&
         y < rect.y + rect.h;
}

bool isPressed(const Button &button) {
  return button.stableLevel == LOW;
}

bool matchOver() {
  if (currentScreen != ScreenMode::Match) {
    return false;
  }

  if (isPadelSport()) {
    return teamA.sets >= 2 || teamB.sets >= 2;
  }

  if (isParetSport()) {
    return teamA.points <= 0 || teamB.points <= 0;
  }

  return false;
}

bool inTieBreak() {
  return currentScreen == ScreenMode::Match && isPadelSport() && !matchOver() &&
         teamA.games == 6 && teamB.games == 6;
}

void setStatus(const char *message) {
  snprintf(statusLine, sizeof(statusLine), "%s", message);
  uiDirty = true;
}

void clearStatus() {
  statusLine[0] = '\0';
  uiDirty = true;
}

void markDirty() {
  uiDirty = true;
  stateDirty = true;
}

unsigned long elapsedTimeMs() {
  if (!timerRunning) {
    return timerAccumulatedMs;
  }
  return timerAccumulatedMs + (millis() - timerStartedAt);
}

String formatElapsedTime(unsigned long elapsedMs) {
  const unsigned long totalSeconds = elapsedMs / 1000UL;
  const unsigned long minutes = totalSeconds / 60UL;
  const unsigned long seconds = totalSeconds % 60UL;

  char buffer[8];
  snprintf(buffer, sizeof(buffer), "%02lu:%02lu", minutes, seconds);
  return String(buffer);
}

String pointLabel(const TeamScore &team, const TeamScore &other) {
  if (matchOver()) {
    if (isPadelSport()) {
      return team.sets > other.sets ? "WIN" : "--";
    }
    if (isParetSport()) {
      return team.points > other.points ? "WIN" : "0";
    }
  }

  if (!isPadelSport()) {
    return String(team.points);
  }

  if (inTieBreak()) {
    return String(team.points);
  }

  if (team.points >= 3 && other.points >= 3) {
    if (team.points == other.points) {
      return "40";
    }
    if (team.points == other.points + 1) {
      return "Ad";
    }
    return "40";
  }

  switch (team.points) {
    case 0:
      return "0";
    case 1:
      return "15";
    case 2:
      return "30";
    default:
      return "40";
  }
}

String statePayload() {
  String json = "{";
  json += "\"sport\":\"";
  json += sportName();
  json += "\",";
  json += "\"teamAPoints\":\"";
  json += pointLabel(teamA, teamB);
  json += "\",\"teamAGames\":";
  json += teamA.games;
  json += ",\"teamASets\":";
  json += teamA.sets;
  json += ",\"teamBPoints\":\"";
  json += pointLabel(teamB, teamA);
  json += "\",\"teamBGames\":";
  json += teamB.games;
  json += ",\"teamBSets\":";
  json += teamB.sets;
  json += ",\"tieBreak\":";
  json += inTieBreak() ? "true" : "false";
  json += ",\"matchOver\":";
  json += matchOver() ? "true" : "false";
  json += "}";
  return json;
}

void goHome() {
  currentScreen = ScreenMode::Home;
  timerRunning = false;
  timerAccumulatedMs = 0;
  timerStartedAt = 0;
  singleClickPending = false;
  lastButtonReleaseAt = 0;
  lastRenderedSecond = ~0UL;
  timerDirty = false;
  clearStatus();
  uiDirty = true;
}

void copyGameLog(char *destination, const char *source) {
  memcpy(destination, source, MAX_GAME_LOG);
}

void captureSnapshot(MatchSnapshot &snapshot) {
  snapshot.teamA = teamA;
  snapshot.teamB = teamB;
  snapshot.timerRunning = timerRunning;
  snapshot.elapsedMs = elapsedTimeMs();
  snapshot.gameLogCount = gameLogCount;
  copyGameLog(snapshot.gameLog, gameLog);
  snapshot.setHistoryCount = setHistoryCount;
  memcpy(snapshot.setScoresA, setScoresA, MAX_SET_HISTORY);
  memcpy(snapshot.setScoresB, setScoresB, MAX_SET_HISTORY);
}

void saveUndoState() {
  captureSnapshot(undoSnapshot);
  hasUndo = true;
}

void restoreSnapshot(const MatchSnapshot &snapshot) {
  teamA = snapshot.teamA;
  teamB = snapshot.teamB;
  gameLogCount = snapshot.gameLogCount;
  copyGameLog(gameLog, snapshot.gameLog);
  setHistoryCount = snapshot.setHistoryCount;
  memcpy(setScoresA, snapshot.setScoresA, MAX_SET_HISTORY);
  memcpy(setScoresB, snapshot.setScoresB, MAX_SET_HISTORY);

  timerRunning = snapshot.timerRunning;
  timerAccumulatedMs = snapshot.elapsedMs;
  timerStartedAt = timerRunning ? millis() : 0;

  singleClickPending = false;
  lastButtonReleaseAt = 0;
  lastRenderedSecond = ~0UL;
  timerDirty = true;
  markDirty();
}

void pushGameLog(char winnerCode) {
  if (gameLogCount < MAX_GAME_LOG) {
    gameLog[gameLogCount++] = winnerCode;
    return;
  }

  memmove(gameLog, gameLog + 1, MAX_GAME_LOG - 1);
  gameLog[MAX_GAME_LOG - 1] = winnerCode;
}

void resetPoints() {
  teamA.points = 0;
  teamB.points = 0;
}

void resetGamesAndPoints() {
  teamA.games = 0;
  teamA.points = 0;
  teamB.games = 0;
  teamB.points = 0;
}

void resetMatch() {
  teamA = {0, 0, 0};
  teamB = {0, 0, 0};
  gameLogCount = 0;
  memset(gameLog, 0, sizeof(gameLog));
  setHistoryCount = 0;
  memset(setScoresA, 0, sizeof(setScoresA));
  memset(setScoresB, 0, sizeof(setScoresB));
  timerRunning = false;
  timerAccumulatedMs = 0;
  timerStartedAt = 0;
  singleClickPending = false;
  lastButtonReleaseAt = 0;
  lastRenderedSecond = ~0UL;
  timerDirty = true;

  if (isParetSport()) {
    teamA.points = 5;
    teamB.points = 5;
  }

  setStatus("Partit reiniciat");
  markDirty();
}

void startSport(SportId sport) {
  currentSport = sport;
  currentScreen = ScreenMode::Match;
  resetMatch();

  switch (sport) {
    case SportId::Padel:
      setStatus("Padel llest");
      break;
    case SportId::Football:
      setStatus("Futbol llest");
      break;
    case SportId::Basketball:
      setStatus("Basquet llest");
      break;
    case SportId::Paret:
      setStatus("Paret llest");
      break;
  }

  lastRenderedSecond = ~0UL;
  timerDirty = true;
  uiDirty = true;
}

void recordSetResult() {
  if (setHistoryCount >= MAX_SET_HISTORY) {
    return;
  }

  setScoresA[setHistoryCount] = static_cast<uint8_t>(teamA.games);
  setScoresB[setHistoryCount] = static_cast<uint8_t>(teamB.games);
  setHistoryCount++;
}

void concludeSet(char winnerCode) {
  recordSetResult();

  if (winnerCode == 'A') {
    teamA.sets++;
    setStatus(teamA.sets >= 2 ? "Partit per A" : "Set per A");
  } else {
    teamB.sets++;
    setStatus(teamB.sets >= 2 ? "Partit per B" : "Set per B");
  }

  resetPoints();

  if (!matchOver()) {
    teamA.games = 0;
    teamB.games = 0;
  }
}

void finishGame(char winnerCode) {
  if (winnerCode == 'A') {
    teamA.games++;
    setStatus("Joc per A");
  } else {
    teamB.games++;
    setStatus("Joc per B");
  }

  pushGameLog(winnerCode);

  if ((max(teamA.games, teamB.games) >= 6 &&
       abs(teamA.games - teamB.games) >= 2) ||
      (max(teamA.games, teamB.games) == 7 &&
       min(teamA.games, teamB.games) == 6)) {
    concludeSet(winnerCode);
    return;
  }

  resetPoints();
}

void awardPoint(TeamScore &winner, TeamScore &loser, char winnerCode) {
  if (matchOver()) {
    setStatus("Partit acabat");
    uiDirty = true;
    return;
  }

  winner.points++;

  if (inTieBreak()) {
    if (winner.points >= 7 && (winner.points - loser.points) >= 2) {
      finishGame(winnerCode);
    } else {
      setStatus(winnerCode == 'A' ? "TB punt A" : "TB punt B");
    }
  } else if (winner.points >= 4 && (winner.points - loser.points) >= 2) {
    finishGame(winnerCode);
  } else {
    setStatus(winnerCode == 'A' ? "Punt A" : "Punt B");
  }

  markDirty();
}

void handlePointA() {
  saveUndoState();
  if (isPadelSport()) {
    awardPoint(teamA, teamB, 'A');
    return;
  }

  if (isParetSport()) {
    if (teamA.points > 0) {
      teamA.points--;
    }
    setStatus(teamA.points <= 0 ? "Perd A" : "Vida -A");
    markDirty();
    return;
  }

  teamA.points++;
  setStatus(currentSport == SportId::Football ? "Gol A" : "Punts A");
  markDirty();
}

void handlePointB() {
  saveUndoState();
  if (isPadelSport()) {
    awardPoint(teamB, teamA, 'B');
    return;
  }

  if (isParetSport()) {
    if (teamB.points > 0) {
      teamB.points--;
    }
    setStatus(teamB.points <= 0 ? "Perd B" : "Vida -B");
    markDirty();
    return;
  }

  teamB.points++;
  setStatus(currentSport == SportId::Football ? "Gol B" : "Punts B");
  markDirty();
}

void toggleTimer() {
  if (timerRunning) {
    timerAccumulatedMs = elapsedTimeMs();
    timerRunning = false;
    timerStartedAt = 0;
    setStatus("Temps en pausa");
  } else {
    timerStartedAt = millis();
    timerRunning = true;
    setStatus("Temps en marxa");
  }

  lastRenderedSecond = ~0UL;
  timerDirty = true;
  uiDirty = true;
}

void undoLastAction() {
  if (!hasUndo) {
    setStatus("Cap canvi a desfer");
    return;
  }

  const MatchSnapshot snapshot = undoSnapshot;
  hasUndo = false;
  restoreSnapshot(snapshot);
  setStatus("Ultim canvi desfet");
}

bool updateButton(Button &button) {
  const bool reading = digitalRead(button.pin);

  if (reading != button.lastReading) {
    button.lastReading = reading;
    button.lastChangeAt = millis();
  }

  if ((millis() - button.lastChangeAt) < DEBOUNCE_MS) {
    return false;
  }

  if (reading == button.stableLevel) {
    return false;
  }

  button.stableLevel = reading;
  return button.stableLevel == HIGH;
}

void updateScoreButton() {
  if (updateButton(scoreButton)) {
    const unsigned long now = millis();
    if (singleClickPending && (now - lastButtonReleaseAt <= DOUBLE_CLICK_MS)) {
      handlePointB();
      singleClickPending = false;
      lastButtonReleaseAt = 0;
    } else {
      singleClickPending = true;
      lastButtonReleaseAt = now;
    }
  }

  if (singleClickPending && !isPressed(scoreButton) &&
      (millis() - lastButtonReleaseAt > DOUBLE_CLICK_MS)) {
    handlePointA();
    singleClickPending = false;
    lastButtonReleaseAt = 0;
  }
}

bool mapTouchToScreen(int16_t &screenX, int16_t &screenY) {
  if (!touch.touched()) {
    return false;
  }

  TS_Point point = touch.getPoint();
  if (point.z < 200) {
    return false;
  }

  screenX = map(point.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_WIDTH);
  screenY = map(point.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_HEIGHT);
  screenX = constrain(screenX, 0, SCREEN_WIDTH - 1);
  screenY = constrain(screenY, 0, SCREEN_HEIGHT - 1);
  return true;
}

void handleTouchAction(int16_t screenX, int16_t screenY) {
  if (currentScreen == ScreenMode::Home) {
    if (contains(HOME_PADEL_RECT, screenX, screenY)) {
      startSport(SportId::Padel);
    } else if (contains(HOME_FOOTBALL_RECT, screenX, screenY)) {
      startSport(SportId::Football);
    } else if (contains(HOME_BASKET_RECT, screenX, screenY)) {
      startSport(SportId::Basketball);
    } else if (contains(HOME_PARET_RECT, screenX, screenY)) {
      startSport(SportId::Paret);
    }
    return;
  }

  if (contains(TIMER_RECT, screenX, screenY) ||
      contains(BTN_TIMER_RECT, screenX, screenY)) {
    toggleTimer();
    return;
  }

  if (contains(BTN_UNDO_RECT, screenX, screenY)) {
    undoLastAction();
    return;
  }

  if (contains(STATUS_RECT, screenX, screenY)) {
    saveUndoState();
    resetMatch();
    return;
  }

  if (contains(LOG_RECT, screenX, screenY)) {
    goHome();
    return;
  }

  if (contains(BTN_A_RECT, screenX, screenY) || contains(CARD_A_RECT, screenX, screenY)) {
    handlePointA();
    return;
  }

  if (contains(BTN_B_RECT, screenX, screenY) || contains(CARD_B_RECT, screenX, screenY)) {
    handlePointB();
  }
}

void updateTouchInput() {
  int16_t screenX = 0;
  int16_t screenY = 0;
  const bool pressed = mapTouchToScreen(screenX, screenY);

  if (!pressed) {
    touchHeld = false;
    return;
  }

  if (touchHeld) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastTouchAt < TOUCH_DEBOUNCE_MS) {
    return;
  }

  touchHeld = true;
  lastTouchAt = now;
  handleTouchAction(screenX, screenY);
}

void drawCenteredText(int16_t centerX, int16_t baselineY, const String &text,
                      uint16_t color, int font, uint16_t backgroundColor) {
  tft.setTextColor(color, backgroundColor);
  tft.drawCentreString(text, centerX, baselineY, font);
}

void drawTextLine(int16_t x, int16_t y, const String &text, uint16_t color,
                  int font, uint16_t backgroundColor) {
  tft.setTextColor(color, backgroundColor);
  tft.drawString(text, x, y, font);
}

void drawPill(const UiRect &rect, uint16_t fillColor, uint16_t borderColor) {
  tft.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 12, fillColor);
  tft.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 12, borderColor);
}

void drawStatusPanel() {
  drawPill(STATUS_MSG_RECT, COLOR_PANEL_ALT, COLOR_LINE);

  if (statusLine[0] == '\0') {
    return;
  }

  String text(statusLine);
  if (text.length() <= 14) {
    drawCenteredText(STATUS_MSG_RECT.x + STATUS_MSG_RECT.w / 2, STATUS_MSG_RECT.y + 8,
                     text, COLOR_TEXT, 1, COLOR_PANEL_ALT);
    return;
  }

  int splitAt = text.lastIndexOf(' ', 14);
  if (splitAt <= 0) {
    splitAt = text.indexOf(' ', 14);
  }
  if (splitAt <= 0) {
    splitAt = 14;
  }

  const String line1 = text.substring(0, splitAt);
  const String line2 = text.substring(splitAt + 1);
  drawCenteredText(STATUS_MSG_RECT.x + STATUS_MSG_RECT.w / 2, STATUS_MSG_RECT.y + 3,
                   line1, COLOR_TEXT, 1, COLOR_PANEL_ALT);
  drawCenteredText(STATUS_MSG_RECT.x + STATUS_MSG_RECT.w / 2, STATUS_MSG_RECT.y + 13,
                   line2, COLOR_TEXT, 1, COLOR_PANEL_ALT);
}

void drawButton(const UiRect &rect, uint16_t fillColor, const String &label,
                int font, int16_t topOffset = 9) {
  drawPill(rect, fillColor, COLOR_LINE);
  drawCenteredText(rect.x + rect.w / 2, rect.y + topOffset, label, COLOR_TEXT, font,
                   fillColor);
}

void drawSportTile(const UiRect &rect, uint16_t fillColor, const String &title,
                   const String &subtitle) {
  drawPill(rect, fillColor, COLOR_LINE);
  drawCenteredText(rect.x + rect.w / 2, rect.y + 8, title, COLOR_BG, 2, fillColor);
  drawCenteredText(rect.x + rect.w / 2, rect.y + 34, subtitle, COLOR_TEXT, 1,
                   fillColor);
}

void drawScoreCard(const UiRect &rect, const char *teamLabel, const String &pointValue,
                   int games, int sets, uint16_t accentColor) {
  const UiRect gamesRect{static_cast<int16_t>(rect.x + 14),
                         static_cast<int16_t>(rect.y + rect.h - 28),
                         static_cast<int16_t>(rect.w - 28), 18};

  tft.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 18, COLOR_PANEL);
  tft.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 18, accentColor);

  tft.fillRoundRect(rect.x + 10, rect.y + 10, rect.w - 20, 22, 10, accentColor);
  drawCenteredText(rect.x + rect.w / 2, rect.y + 14, teamLabel, COLOR_BG, 2,
                   accentColor);

  const int bigFont = pointValue.length() > 2 ? 2 : 4;
  const int16_t bigValueY = 92;
  const uint16_t pointColor = COLOR_TEXT;
  drawCenteredText(rect.x + rect.w / 2, bigValueY, pointValue, pointColor, bigFont,
                   COLOR_PANEL);

  if (isPadelSport()) {
    tft.fillRoundRect(gamesRect.x, gamesRect.y, gamesRect.w, gamesRect.h, 8,
                      COLOR_PANEL_ALT);
    drawCenteredText(gamesRect.x + 28, gamesRect.y + 3, "SETS", COLOR_MUTED, 1,
                     COLOR_PANEL_ALT);
    drawCenteredText(gamesRect.x + 28, gamesRect.y + 11, String(sets), COLOR_TEXT, 2,
                     COLOR_PANEL_ALT);
    drawCenteredText(gamesRect.x + gamesRect.w - 28, gamesRect.y + 3, "JOCS",
                     COLOR_MUTED, 1, COLOR_PANEL_ALT);
    drawCenteredText(gamesRect.x + gamesRect.w - 28, gamesRect.y + 11,
                     String(games), COLOR_TEXT, 2, COLOR_PANEL_ALT);
    return;
  }

  tft.fillRoundRect(gamesRect.x + 20, gamesRect.y, gamesRect.w - 40, gamesRect.h, 8,
                    COLOR_PANEL_ALT);
  drawCenteredText(rect.x + rect.w / 2, gamesRect.y + 7,
                   currentSport == SportId::Football
                       ? "GOLS"
                       : (currentSport == SportId::Paret ? "VIDES" : "PUNTS"),
                   COLOR_MUTED, 1, COLOR_PANEL_ALT);
}

String setsSummary() {
  if (setHistoryCount == 0) {
    return String(matchOver() ? "final" : "set 1 en joc");
  }

  String summary;
  for (uint8_t i = 0; i < setHistoryCount; ++i) {
    if (i > 0) {
      summary += "  ";
    }
    summary += String(setScoresA[i]);
    summary += "-";
    summary += String(setScoresB[i]);
  }
  return summary;
}

void drawMatchStrip() {
  if (!isPadelSport()) {
    tft.fillRoundRect(LOG_RECT.x, LOG_RECT.y, LOG_RECT.w, LOG_RECT.h, 14,
                      COLOR_PANEL);
    tft.drawRoundRect(LOG_RECT.x, LOG_RECT.y, LOG_RECT.w, LOG_RECT.h, 14,
                      COLOR_LINE);
    drawTextLine(LOG_RECT.x + 10, LOG_RECT.y + 11, sportName(), COLOR_TEXT, 1,
                 COLOR_PANEL);
    drawTextLine(LOG_RECT.x + 168, LOG_RECT.y + 11, "toca per canviar",
                 COLOR_MUTED, 1, COLOR_PANEL);
    return;
  }

  tft.fillRoundRect(LOG_RECT.x, LOG_RECT.y, LOG_RECT.w, LOG_RECT.h, 14,
                    COLOR_PANEL);
  tft.drawRoundRect(LOG_RECT.x, LOG_RECT.y, LOG_RECT.w, LOG_RECT.h, 14,
                    COLOR_LINE);
  drawTextLine(LOG_RECT.x + 10, LOG_RECT.y + 11, "SETS", COLOR_MUTED, 1,
               COLOR_PANEL);
  drawTextLine(LOG_RECT.x + 48, LOG_RECT.y + 11, setsSummary(), COLOR_TEXT, 1,
               COLOR_PANEL);

  if (matchOver()) {
    const char *winner = teamA.sets > teamB.sets ? "GUANYA A" : "GUANYA B";
    drawTextLine(LOG_RECT.x + 210, LOG_RECT.y + 11, winner, COLOR_SUCCESS, 1,
                 COLOR_PANEL);
    return;
  }

  if (inTieBreak()) {
    drawTextLine(LOG_RECT.x + 214, LOG_RECT.y + 11,
                 "TB " + String(teamA.points) + "-" + String(teamB.points),
                 COLOR_TIMER, 1, COLOR_PANEL);
    return;
  }

  drawTextLine(LOG_RECT.x + 204, LOG_RECT.y + 11,
               String("set ") + String(setHistoryCount + 1),
               COLOR_MUTED, 1, COLOR_PANEL);
}

void renderHomeScreen() {
  tft.startWrite();
  tft.fillScreen(COLOR_BG);

  drawCenteredText(HOME_TITLE_RECT.x + HOME_TITLE_RECT.w / 2, HOME_TITLE_RECT.y + 3,
                   "TRIA ESPORT", COLOR_TEXT, 2, COLOR_BG);
  drawCenteredText(HOME_SUBTITLE_RECT.x + HOME_SUBTITLE_RECT.w / 2,
                   HOME_SUBTITLE_RECT.y + 2, "toca per obrir el marcador",
                   COLOR_MUTED, 1, COLOR_BG);

  drawSportTile(HOME_PADEL_RECT, COLOR_A, "PADEL", "punts, jocs i sets");
  drawSportTile(HOME_FOOTBALL_RECT, COLOR_B, "FUTBOL", "marcador directe");
  drawSportTile(HOME_BASKET_RECT, COLOR_TIMER, "BASQUET", "marcador directe");
  drawSportTile(HOME_PARET_RECT, COLOR_SUCCESS, "PARET", "5 vides per costat");

  tft.endWrite();
  uiDirty = false;
  timerDirty = false;
}

void drawTimerPanel() {
  drawPill(TIMER_RECT, timerRunning ? COLOR_TIMER : COLOR_PANEL_ALT, COLOR_TIMER);
  drawCenteredText(TIMER_RECT.x + TIMER_RECT.w / 2, TIMER_RECT.y + 6,
                   formatElapsedTime(elapsedTimeMs()),
                   timerRunning ? COLOR_BG : COLOR_TEXT, 2,
                   timerRunning ? COLOR_TIMER : COLOR_PANEL_ALT);
}

void renderDisplay() {
  if (!uiDirty) {
    return;
  }

  if (currentScreen == ScreenMode::Home) {
    renderHomeScreen();
    return;
  }

  tft.startWrite();
  tft.fillScreen(COLOR_BG);

  drawPill(STATUS_RECT, COLOR_DANGER, COLOR_DANGER);
  drawCenteredText(STATUS_RECT.x + STATUS_RECT.w / 2, STATUS_RECT.y + 10,
                   "RESET", COLOR_TEXT, 1, COLOR_DANGER);

  drawTimerPanel();
  drawStatusPanel();

  drawScoreCard(CARD_A_RECT, isPadelSport() ? "PARELLA A" : "EQUIP A",
                pointLabel(teamA, teamB), teamA.games,
                teamA.sets, COLOR_A);
  drawScoreCard(CARD_B_RECT, isPadelSport() ? "PARELLA B" : "EQUIP B",
                pointLabel(teamB, teamA), teamB.games,
                teamB.sets, COLOR_B);

  drawMatchStrip();

  drawButton(BTN_A_RECT, COLOR_A, "+A", 2);
  drawButton(BTN_UNDO_RECT, COLOR_WARNING, "UNDO", 1, 13);
  drawButton(BTN_TIMER_RECT, timerRunning ? COLOR_TIMER : COLOR_PANEL_ALT,
             timerRunning ? "PAUSA" : "TEMPS", 1, 13);
  drawButton(BTN_B_RECT, COLOR_B, "+B", 2);

  tft.endWrite();
  uiDirty = false;
  timerDirty = false;
}

void renderTimerIfNeeded() {
  if (currentScreen != ScreenMode::Match || !timerDirty || uiDirty) {
    return;
  }

  tft.startWrite();
  drawTimerPanel();
  tft.endWrite();
  timerDirty = false;
}

void publishState() {
  if (!stateDirty || stateCharacteristic == nullptr) {
    return;
  }

  const String payload = statePayload();
  stateCharacteristic->setValue(payload.c_str());

  if (deviceConnected) {
    stateCharacteristic->notify();
  }

  Serial.print("State: ");
  Serial.println(payload);
  stateDirty = false;
}

class ScoreServerCallbacks : public BLEServerCallbacks {
 public:
  void onConnect(BLEServer *server) override {
    deviceConnected = true;
    restartAdvertising = false;
    setStatus("Android connectat");
    stateDirty = true;
    Serial.println("Android connected");
  }

  void onDisconnect(BLEServer *server) override {
    deviceConnected = false;
    restartAdvertising = true;
    setStatus("Android fora");
    Serial.println("Android disconnected");
  }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
 public:
  void onWrite(BLECharacteristic *characteristic) override {
    const std::string value = characteristic->getValue();
    if (value.empty()) {
      return;
    }

    const char command =
        static_cast<char>(toupper(static_cast<unsigned char>(value[0])));
    switch (command) {
      case 'A':
      case '1':
        handlePointA();
        Serial.println("BLE command: point A");
        break;
      case 'B':
      case '2':
        handlePointB();
        Serial.println("BLE command: point B");
        break;
      case 'R':
      case '0':
        saveUndoState();
        resetMatch();
        Serial.println("BLE command: reset");
        break;
      default:
        Serial.print("Unknown BLE command: ");
        Serial.println(command);
        return;
    }

    publishState();
    renderDisplay();
  }
};

void startBle() {
  BLEDevice::init(DEVICE_NAME);
  BLEDevice::setPower(ESP_PWR_LVL_P9);

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ScoreServerCallbacks());

  BLEService *service = server->createService(SERVICE_UUID);

  stateCharacteristic = service->createCharacteristic(
      STATE_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  stateCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic *commandCharacteristic = service->createCharacteristic(
      COMMAND_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  commandCharacteristic->setCallbacks(new CommandCallbacks());

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println();
  Serial.print("BLE device: ");
  Serial.println(DEVICE_NAME);
}

void startDisplay() {
  pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(TFT_BACKLIGHT_PIN, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(false);
  tft.fillScreen(COLOR_BG);
  renderDisplay();

  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSpi);
  touch.setRotation(1);

  Serial.println("Display ready");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(scoreButton.pin, INPUT_PULLUP);

  startDisplay();
  if (ENABLE_BLE) {
    startBle();
  }
  publishState();
  renderDisplay();
}

void loop() {
  if (currentScreen == ScreenMode::Match) {
    updateScoreButton();
  }
  updateTouchInput();

  if (currentScreen == ScreenMode::Match) {
    const unsigned long currentSecond = elapsedTimeMs() / 1000UL;
    if (currentSecond != lastRenderedSecond) {
      lastRenderedSecond = currentSecond;
      timerDirty = true;
    }
  }

  publishState();
  renderDisplay();
  renderTimerIfNeeded();

  if (ENABLE_BLE && restartAdvertising) {
    BLEDevice::startAdvertising();
    restartAdvertising = false;
    Serial.println("BLE advertising restarted");
  }

  delay(5);
}
