// Robo Rumble - ESP32-C6 Bluetooth (BLE) WASD motor controller
//
// A standalone alternative to the WiFi build: no laptop server, no router,
// no phone. Chrome on the laptop connects DIRECTLY to this robot over
// Bluetooth Low Energy (Web Bluetooth API), and W/A/S/D on the laptop's own
// keyboard drives the two motors.
//
// The ESP32-C6 only supports Bluetooth Low Energy, not classic Bluetooth -
// that's why this uses BLE (via NimBLE) rather than a simple serial-port-
// style Bluetooth connection.
//
// Reuses the exact same motor wiring as the WiFi build:
//   Motor A (left):  ENA=GPIO6,  IN1=GPIO7,  IN2=GPIO10
//   Motor B (right): ENB=GPIO20, IN3=GPIO22, IN4=GPIO21
//
// SETUP (Arduino IDE):
//   1. Boards Manager -> "esp32" by Espressif Systems, 3.0.0+, board "ESP32C6 Dev Module"
//   2. Library Manager -> install "NimBLE-Arduino" by h2zero (latest 2.x).
//      This is the most reliable BLE library across ESP32 chips including the C6 -
//      the older/built-in "BLEDevice.h" library from classic ESP32 sketches is not
//      used here.
//   3. Change DEVICE_NAME below if you're running more than one robot, so you
//      can tell them apart in Chrome's "pair" list.
//   4. Upload. No WiFi credentials needed for this build at all.
//
// Then open controller/control.html in Chrome (or Edge) on the laptop,
// click Connect, pick this robot from the pairing popup, and hold W/A/S/D.
//
// SAFETY: if the BLE link drops, or the browser stops sending updates for
// any reason, both motors are forced to stop after MOTOR_WATCHDOG_MS - see
// the watchdog check in loop() - and also immediately on BLE disconnect.
//
// POWER NOTE: motors need their own separate supply into the L298N (not the
// ESP32's USB power), sharing only GND with the ESP32. Even then, the
// current inrush when a motor starts can sag the rail enough to brown out
// the ESP32 - if that happens, add a bulk capacitor (470-1000uF) across the
// L298N's motor power input, right at its terminals. The soft-start ramp
// below (RAMP_STEP/RAMP_INTERVAL_MS) also helps by not jumping straight to
// full power, but it doesn't replace an under-built power supply.

#include <NimBLEDevice.h>

const char* DEVICE_NAME = "RoboRumble-ESP1"; // shown in Chrome's Bluetooth pairing list

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

// ---- motor A (left) pins ----
#define PIN_ENA 6
#define PIN_IN1 7
#define PIN_IN2 10

// ---- motor B (right) pins ----
#define PIN_ENB 20
#define PIN_IN3 22
#define PIN_IN4 21

const unsigned long MOTOR_WATCHDOG_MS = 400; // stop both motors if no command arrives within this window

// ---- soft-start ramp ----
// Steps the actual output toward the target speed instead of jumping
// straight there, so a full-power command doesn't slam the motor (and the
// power rail) with its full inrush current all at once.
const int RAMP_STEP = 15;                  // max PWM change per ramp tick (0-255 in ~170ms)
const unsigned long RAMP_INTERVAL_MS = 10;

NimBLECharacteristic* rxCharacteristic = nullptr;

int targetASpeed = 0;                // last commanded target, -255..255
int targetBSpeed = 0;
int currentASpeed = 0;               // actually applied right now - ramps toward the target
int currentBSpeed = 0;
unsigned long lastMotorCmdMs = 0;
unsigned long lastRampMs = 0;

void driveMotorA(int speed) {
  if (speed > 0) {
    digitalWrite(PIN_IN1, HIGH);
    digitalWrite(PIN_IN2, LOW);
    analogWrite(PIN_ENA, speed);
  } else if (speed < 0) {
    digitalWrite(PIN_IN1, LOW);
    digitalWrite(PIN_IN2, HIGH);
    analogWrite(PIN_ENA, -speed);
  } else {
    digitalWrite(PIN_IN1, LOW);
    digitalWrite(PIN_IN2, LOW);
    analogWrite(PIN_ENA, 0);
  }
}

void driveMotorB(int speed) {
  if (speed > 0) {
    digitalWrite(PIN_IN3, HIGH);
    digitalWrite(PIN_IN4, LOW);
    analogWrite(PIN_ENB, speed);
  } else if (speed < 0) {
    digitalWrite(PIN_IN3, LOW);
    digitalWrite(PIN_IN4, HIGH);
    analogWrite(PIN_ENB, -speed);
  } else {
    digitalWrite(PIN_IN3, LOW);
    digitalWrite(PIN_IN4, LOW);
    analogWrite(PIN_ENB, 0);
  }
}

void setTargetA(int speed) {
  targetASpeed = constrain(speed, -255, 255);
}

void setTargetB(int speed) {
  targetBSpeed = constrain(speed, -255, 255);
}

void stopBothMotors() {
  targetASpeed = 0;
  targetBSpeed = 0;
  currentASpeed = 0;
  currentBSpeed = 0;
  driveMotorA(0);
  driveMotorB(0);
}

int stepToward(int current, int target, int maxStep) {
  if (current < target) return min(current + maxStep, target);
  if (current > target) return max(current - maxStep, target);
  return current;
}

void rampMotors() {
  unsigned long now = millis();
  if (now - lastRampMs < RAMP_INTERVAL_MS) return;
  lastRampMs = now;

  int newA = stepToward(currentASpeed, targetASpeed, RAMP_STEP);
  if (newA != currentASpeed) {
    currentASpeed = newA;
    driveMotorA(currentASpeed);
  }
  int newB = stepToward(currentBSpeed, targetBSpeed, RAMP_STEP);
  if (newB != currentBSpeed) {
    currentBSpeed = newB;
    driveMotorB(currentBSpeed);
  }
}

// Parses "M:<left>,<right>" e.g. "M:180,-90"
void handleCommand(const String& msg) {
  if (!msg.startsWith("M:")) return;
  int comma = msg.indexOf(',', 2);
  if (comma == -1) return;
  int left = msg.substring(2, comma).toInt();
  int right = msg.substring(comma + 1).toInt();
  setTargetA(left);
  setTargetB(right);
  lastMotorCmdMs = millis();
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    Serial.println("[BLE] client connected");
  }
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    Serial.println("[BLE] client disconnected");
    stopBothMotors(); // safety: stop immediately if the link drops
    NimBLEDevice::startAdvertising();
  }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
    handleCommand(String(pChar->getValue().c_str()));
  }
};

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_ENA, OUTPUT);
  pinMode(PIN_IN3, OUTPUT);
  pinMode(PIN_IN4, OUTPUT);
  pinMode(PIN_ENB, OUTPUT);
  stopBothMotors();

  NimBLEDevice::init(DEVICE_NAME);

  NimBLEServer* pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService* pService = pServer->createService(SERVICE_UUID);
  rxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  rxCharacteristic->setCallbacks(new RxCallbacks());

  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();

  Serial.print("[BLE] advertising as ");
  Serial.println(DEVICE_NAME);
}

void loop() {
  rampMotors();

  // Safety watchdog: if a motor is running (or ramping toward a nonzero
  // target) but no command has arrived recently (BLE hiccup, browser tab
  // closed, laptop asleep, etc.), force both motors to stop rather than
  // letting them run away unattended.
  bool motorsActive = targetASpeed != 0 || targetBSpeed != 0 || currentASpeed != 0 || currentBSpeed != 0;
  if (motorsActive && (millis() - lastMotorCmdMs) > MOTOR_WATCHDOG_MS) {
    Serial.println("[SAFETY] motor watchdog: no recent command, stopping");
    stopBothMotors();
  }
  delay(10);
}
