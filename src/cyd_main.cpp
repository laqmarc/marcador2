#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

namespace {

constexpr uint8_t SCORE_BUTTON_PIN = 0;
constexpr unsigned long DEBOUNCE_MS = 35;
constexpr unsigned long DOUBLE_CLICK_MS = 320;

constexpr char DEVICE_NAME[] = "MarcadorPadel-BLE";
constexpr char SERVICE_UUID[] = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
constexpr char STATE_UUID[] = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
constexpr char COMMAND_UUID[] = "e3223119-9445-4e96-a4a1-85358c4046a2";

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
constexpr uint16_t TOUCH_DEBOUNCE_MS = 250;

constexpr uint16_t SCREEN_WIDTH = 320;
constexpr uint16_t SCREEN_HEIGHT = 240;

constexpr uint16_t COLOR_BG = TFT_NAVY;
constexpr uint16_t COLOR_PANEL = 0x18E3;
constexpr uint16_t COLOR_LINE = 0x4228;
constexpr uint16_t COLOR_TEXT = TFT_WHITE;
constexpr uint16_t COLOR_MUTED = 0x9CD3;
constexpr uint16_t COLOR_A = 0xFD20;
constexpr uint16_t COLOR_B = 0x867D;
constexpr uint16_t COLOR_ACCENT = 0x4FEA;
constexpr uint16_t COLOR_OFF = 0x2945;

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

struct Button {
  uint8_t pin;
  bool stableLevel;
  bool lastReading;
  unsigned long lastChangeAt;
};

struct TeamScore {
  int games;
  int points;
};

Button scoreButton{SCORE_BUTTON_PIN, HIGH, HIGH, 0};
TeamScore teamA{0, 0};
TeamScore teamB{0, 0};

bool singleClickPending = false;
unsigned long lastButtonReleaseAt = 0;
bool uiDirty = true;
bool stateDirty = true;
bool touchHeld = false;
unsigned long lastTouchAt = 0;

BLECharacteristic *stateCharacteristic = nullptr;
bool deviceConnected = false;
bool restartAdvertising = false;

bool isPressed(const Button &button) {
  return button.stableLevel == LOW;
}

void markDirty() {
  uiDirty = true;
  stateDirty = true;
}

const char *pointLabel(const TeamScore &team, const TeamScore &other) {
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
  json += "\"teamAPoints\":\"";
  json += pointLabel(teamA, teamB);
  json += "\",\"teamAGames\":";
  json += teamA.games;
  json += ",\"teamBPoints\":\"";
  json += pointLabel(teamB, teamA);
  json += "\",\"teamBGames\":";
  json += teamB.games;
  json += "}";
  return json;
}

void resetPoints() {
  teamA.points = 0;
  teamB.points = 0;
}

void resetScores() {
  teamA.games = 0;
  teamA.points = 0;
  teamB.games = 0;
  teamB.points = 0;
  singleClickPending = false;
  lastButtonReleaseAt = 0;
  markDirty();
}

void awardPoint(TeamScore &winner, TeamScore &loser) {
  winner.points++;

  if (winner.points >= 4 && (winner.points - loser.points) >= 2) {
    winner.games++;
    resetPoints();
  }

  markDirty();
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
      awardPoint(teamB, teamA);
      singleClickPending = false;
      lastButtonReleaseAt = 0;
    } else {
      singleClickPending = true;
      lastButtonReleaseAt = now;
    }
  }

  if (singleClickPending && !isPressed(scoreButton) &&
      (millis() - lastButtonReleaseAt > DOUBLE_CLICK_MS)) {
    awardPoint(teamA, teamB);
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

  if (screenX < SCREEN_WIDTH / 2) {
    awardPoint(teamA, teamB);
    Serial.println("Touch point A");
  } else {
    awardPoint(teamB, teamA);
    Serial.println("Touch point B");
  }
}

struct SegmentMask {
  char key;
  uint8_t mask;
};

constexpr uint8_t SEG_A = 1 << 0;
constexpr uint8_t SEG_B = 1 << 1;
constexpr uint8_t SEG_C = 1 << 2;
constexpr uint8_t SEG_D = 1 << 3;
constexpr uint8_t SEG_E = 1 << 4;
constexpr uint8_t SEG_F = 1 << 5;
constexpr uint8_t SEG_G = 1 << 6;

constexpr SegmentMask SEGMENT_MAP[] = {
    {'0', SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F},
    {'1', SEG_B | SEG_C},
    {'2', SEG_A | SEG_B | SEG_D | SEG_E | SEG_G},
    {'3', SEG_A | SEG_B | SEG_C | SEG_D | SEG_G},
    {'4', SEG_B | SEG_C | SEG_F | SEG_G},
    {'5', SEG_A | SEG_C | SEG_D | SEG_F | SEG_G},
    {'6', SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G},
    {'7', SEG_A | SEG_B | SEG_C},
    {'8', SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G},
    {'9', SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G},
    {'A', SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G},
    {'b', SEG_C | SEG_D | SEG_E | SEG_F | SEG_G},
    {'d', SEG_B | SEG_C | SEG_D | SEG_E | SEG_G},
    {' ', 0},
};

uint8_t segmentMaskFor(char c) {
  for (const auto &entry : SEGMENT_MAP) {
    if (entry.key == c) {
      return entry.mask;
    }
  }
  return 0;
}

void drawSegmentDisplay(int16_t x, int16_t y, int16_t w, int16_t h, char value,
                        uint16_t onColor) {
  const uint8_t mask = segmentMaskFor(value);
  const int16_t t = max<int16_t>(4, w / 6);
  const int16_t vH = (h - 3 * t) / 2;
  const uint16_t offColor = COLOR_OFF;

  auto horizontal = [&](int16_t sx, int16_t sy, bool on) {
    tft.fillRoundRect(sx, sy, w - 2 * t, t, t / 2, on ? onColor : offColor);
  };

  auto vertical = [&](int16_t sx, int16_t sy, bool on) {
    tft.fillRoundRect(sx, sy, t, vH, t / 2, on ? onColor : offColor);
  };

  horizontal(x + t, y, mask & SEG_A);
  vertical(x + w - t, y + t, mask & SEG_B);
  vertical(x + w - t, y + 2 * t + vH, mask & SEG_C);
  horizontal(x + t, y + h - t, mask & SEG_D);
  vertical(x, y + 2 * t + vH, mask & SEG_E);
  vertical(x, y + t, mask & SEG_F);
  horizontal(x + t, y + t + vH, mask & SEG_G);
}

String pointCode(const TeamScore &team, const TeamScore &other) {
  const char *label = pointLabel(team, other);
  if (strcmp(label, "0") == 0) {
    return " 0";
  }
  if (strcmp(label, "15") == 0) {
    return "15";
  }
  if (strcmp(label, "30") == 0) {
    return "30";
  }
  if (strcmp(label, "40") == 0) {
    return "40";
  }
  return "Ad";
}

void drawScorePair(int16_t x, int16_t y, const String &value, uint16_t color,
                   int16_t cellW, int16_t cellH, int16_t gap) {
  const char left = value.length() > 0 ? value[0] : ' ';
  const char right = value.length() > 1 ? value[1] : ' ';
  drawSegmentDisplay(x, y, cellW, cellH, left, color);
  drawSegmentDisplay(x + cellW + gap, y, cellW, cellH, right, color);
}

void drawGamesRow(int16_t x, int16_t y, int value, uint16_t color) {
  const int capped = constrain(value, 0, 99);
  const char tens = capped >= 10 ? static_cast<char>('0' + capped / 10) : ' ';
  const char ones = static_cast<char>('0' + capped % 10);
  drawSegmentDisplay(x, y, 26, 46, tens, color);
  drawSegmentDisplay(x + 34, y, 26, 46, ones, color);
}

void renderDisplay() {
  if (!uiDirty) {
    return;
  }

  tft.startWrite();
  tft.fillScreen(COLOR_BG);

  tft.fillRect(0, 0, SCREEN_WIDTH / 2, SCREEN_HEIGHT, COLOR_A);
  tft.fillRect(SCREEN_WIDTH / 2, 0, SCREEN_WIDTH / 2, SCREEN_HEIGHT, COLOR_B);
  tft.drawFastVLine(SCREEN_WIDTH / 2, 0, SCREEN_HEIGHT, COLOR_TEXT);

  drawScorePair(18, 42, pointCode(teamA, teamB), COLOR_TEXT, 50, 96, 12);
  drawScorePair(180, 42, pointCode(teamB, teamA), COLOR_TEXT, 50, 96, 12);

  tft.fillRoundRect(42, 168, 76, 52, 10, COLOR_PANEL);
  tft.fillRoundRect(202, 168, 76, 52, 10, COLOR_PANEL);
  drawGamesRow(50, 172, teamA.games, COLOR_TEXT);
  drawGamesRow(210, 172, teamB.games, COLOR_TEXT);
  tft.endWrite();

  uiDirty = false;
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
    uiDirty = true;
    stateDirty = true;
    Serial.println("Android connected");
  }

  void onDisconnect(BLEServer *server) override {
    deviceConnected = false;
    restartAdvertising = true;
    uiDirty = true;
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

    const char command = static_cast<char>(toupper(static_cast<unsigned char>(value[0])));
    switch (command) {
      case 'A':
      case '1':
        awardPoint(teamA, teamB);
        Serial.println("BLE command: point A");
        break;
      case 'B':
      case '2':
        awardPoint(teamB, teamA);
        Serial.println("BLE command: point B");
        break;
      case 'R':
      case '0':
        resetScores();
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
  startBle();
  publishState();
  renderDisplay();
}

void loop() {
  updateScoreButton();
  updateTouchInput();
  publishState();
  renderDisplay();

  if (restartAdvertising) {
    BLEDevice::startAdvertising();
    restartAdvertising = false;
    Serial.println("BLE advertising restarted");
  }

  delay(5);
}
