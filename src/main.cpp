#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

namespace {

constexpr uint8_t SCORE_BUTTON_PIN = 9;
constexpr unsigned long DEBOUNCE_MS = 35;
constexpr unsigned long DOUBLE_CLICK_MS = 320;

constexpr char DEVICE_NAME[] = "MarcadorPadel-BLE";
constexpr char SERVICE_UUID[] = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
constexpr char STATE_UUID[] = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
constexpr char COMMAND_UUID[] = "e3223119-9445-4e96-a4a1-85358c4046a2";

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

BLECharacteristic *stateCharacteristic = nullptr;
bool deviceConnected = false;
bool restartAdvertising = false;
bool stateDirty = true;

bool isPressed(const Button &button) {
  return button.stableLevel == LOW;
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

void markStateDirty() {
  stateDirty = true;
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
  markStateDirty();
}

void awardPoint(TeamScore &winner, TeamScore &loser) {
  winner.points++;

  if (winner.points >= 4 && (winner.points - loser.points) >= 2) {
    winner.games++;
    resetPoints();
  }

  markStateDirty();
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
    Serial.println("Android connected");
    markStateDirty();
  }

  void onDisconnect(BLEServer *server) override {
    deviceConnected = false;
    restartAdvertising = true;
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
  Serial.print("Service UUID: ");
  Serial.println(SERVICE_UUID);
  Serial.print("State UUID: ");
  Serial.println(STATE_UUID);
  Serial.print("Command UUID: ");
  Serial.println(COMMAND_UUID);
  Serial.println("Commands: A, B, R");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(scoreButton.pin, INPUT_PULLUP);

  startBle();
  publishState();
}

void loop() {
  updateScoreButton();
  publishState();

  if (restartAdvertising) {
    BLEDevice::startAdvertising();
    restartAdvertising = false;
    Serial.println("BLE advertising restarted");
  }

  delay(5);
}
