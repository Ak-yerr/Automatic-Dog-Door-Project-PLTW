/*
 * dog_door.ino
 *
 * standalone mode:   full door operation, no logging
 * pc-enhanced mode:  auto-detected on startup via PING/PONG handshake
 *
 * serial protocol (9600 baud):
 *   arduino → pc :  "PING", "OPEN|<uid>", "DENIED|<uid>", "CLOSED"
 *   pc      → arduino:  "PONG" (handshake), "OVERRIDE" (web ui button)
 *
 * led states:
 *   red on, yellow off  = closed / idle
 *   red off, yellow on  = open
 *   red blinking        = denied scan
 *
 * stepper position convention (AccelStepper absolute):
 *   0           = door closed  (home)
 *   DOOR_STEPS  = door fully open
 */

#include <SPI.h>
#include <MFRC522.h>
#include <AccelStepper.h>

// ── pins ──────────────────────────────────────────────────────────────────────
#define PIR_PIN       2
#define STEP_PIN      3
#define DIR_PIN       4
#define LED_RED       5
#define BTN_PIN       6     // momentary NO pushbutton, other leg to GND
#define RFID_RST_PIN  9
#define RFID_SS_PIN   10
#define LED_YELLOW    A3

// ── tuning ───────────────────────────────────────────────────────────────────
#define DOOR_STEPS      200       // steps for full open — increase if door doesn't fully open
#define STEPPER_SPEED   500       // steps/sec
#define STEPPER_ACCEL   200       // steps/sec²

#define RFID_WINDOW     10000UL   // ms to wait for tag after motion
#define DOOR_HOLD       5000UL    // minimum time door stays open
#define PIR_RECHECK     2000UL    // extra wait if pir still active when closing
#define PC_TIMEOUT      2500UL    // ms to wait for PONG on startup
#define BTN_DEBOUNCE    50UL      // ms debounce for manual button

// ── authorized uids ──────────────────────────────────────────────────────────
// upload uid_scan.ino first, scan your dog's tag, paste the bytes here
const byte AUTHORIZED_TAGS[][4] = {
  { 0xDE, 0xAD, 0xBE, 0xEF },  // replace with real uid
};
const int TAG_COUNT = sizeof(AUTHORIZED_TAGS) / sizeof(AUTHORIZED_TAGS[0]);

// ── objects ───────────────────────────────────────────────────────────────────
AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);
MFRC522      rfid(RFID_SS_PIN, RFID_RST_PIN);

// ── state machine ─────────────────────────────────────────────────────────────
enum State { IDLE, WAITING_FOR_RFID, DOOR_OPEN, CLOSING };
State        state          = IDLE;
unsigned long stateEnteredAt = 0;

// ── flags ─────────────────────────────────────────────────────────────────────
bool          pcConnected    = false;
bool          overridePending = false;

// ── button debounce ───────────────────────────────────────────────────────────
bool          lastBtnReading  = HIGH;
unsigned long btnChangedAt    = 0;
bool          btnState        = HIGH;   // debounced state

// ─────────────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();

  pinMode(PIR_PIN,    INPUT);
  pinMode(LED_RED,    OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(BTN_PIN,    INPUT_PULLUP);

  stepper.setMaxSpeed(STEPPER_SPEED);
  stepper.setAcceleration(STEPPER_ACCEL);
  stepper.setCurrentPosition(0);   // declare closed as home

  setLeds(HIGH, LOW);

  // handshake — blocks for up to PC_TIMEOUT ms, then continues standalone
  Serial.println("PING");
  unsigned long t = millis();
  while (millis() - t < PC_TIMEOUT) {
    if (Serial.available()) {
      String r = Serial.readStringUntil('\n');
      r.trim();
      if (r == "PONG") { pcConnected = true; break; }
    }
  }
  while (Serial.available()) Serial.read();   // flush any junk
}

void loop() {
  stepper.run();    // must be called every iteration
  checkSerial();
  checkButton();

  switch (state) {

    case IDLE:
      if (overridePending) {
        overridePending = false;
        triggerOpen("OVERRIDE");
        break;
      }
      if (digitalRead(PIR_PIN) == HIGH) {
        state = WAITING_FOR_RFID;
        stateEnteredAt = millis();
      }
      break;

    case WAITING_FOR_RFID:
      if (overridePending) {
        overridePending = false;
        triggerOpen("OVERRIDE");
        break;
      }
      if (millis() - stateEnteredAt > RFID_WINDOW) {
        state = IDLE;   // timed out
        break;
      }
      if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
        String uid = uidToString(rfid.uid.uidByte, rfid.uid.size);
        if (isAuthorized(rfid.uid.uidByte, rfid.uid.size)) {
          triggerOpen(uid);
        } else {
          if (pcConnected) Serial.println("DENIED|" + uid);
          blinkRed(3);
        }
        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();
      }
      break;

    case DOOR_OPEN:
      overridePending = false;   // already open, discard
      if (millis() - stateEnteredAt >= DOOR_HOLD) {
        state = CLOSING;
        stateEnteredAt = millis();
      }
      break;

    case CLOSING:
      // pir still sees the dog — extend wait, don't issue another move()
      if (digitalRead(PIR_PIN) == HIGH) {
        stateEnteredAt = millis() + PIR_RECHECK;
        break;
      }

      // issue the close move exactly once (distanceToGo == 0 and position != 0)
      if (stepper.distanceToGo() == 0 && stepper.currentPosition() != 0) {
        stepper.moveTo(0);   // absolute home — same mode as open, no conflict
      }

      // fully closed when motor has stopped at home
      if (stepper.distanceToGo() == 0 && stepper.currentPosition() == 0) {
        setLeds(HIGH, LOW);
        if (pcConnected) Serial.println("CLOSED");
        state = IDLE;
      }
      break;
  }
}

// ── helpers ───────────────────────────────────────────────────────────────────

void triggerOpen(String uid) {
  setLeds(LOW, HIGH);
  stepper.moveTo(DOOR_STEPS);
  if (pcConnected) Serial.println("OPEN|" + uid);
  state = DOOR_OPEN;
  stateEnteredAt = millis();
}

void checkSerial() {
  if (!Serial.available()) return;
  String msg = Serial.readStringUntil('\n');
  msg.trim();
  if (msg == "OVERRIDE")   overridePending = true;
  if (msg == "PONG")       pcConnected     = true;   // late handshake if server starts after boot
}

void checkButton() {
  bool reading = digitalRead(BTN_PIN);
  if (reading != lastBtnReading) btnChangedAt = millis();
  lastBtnReading = reading;

  if (millis() - btnChangedAt >= BTN_DEBOUNCE) {
    if (reading == LOW && btnState == HIGH) {
      // confirmed press
      overridePending = true;
    }
    btnState = reading;
  }
}

bool isAuthorized(byte *uid, byte uidSize) {
  if (uidSize != 4) return false;
  for (int i = 0; i < TAG_COUNT; i++) {
    if (memcmp(uid, AUTHORIZED_TAGS[i], 4) == 0) return true;
  }
  return false;
}

String uidToString(byte *uid, byte len) {
  String s = "";
  for (byte i = 0; i < len; i++) {
    if (uid[i] < 0x10) s += "0";
    s += String(uid[i], HEX);
    if (i < len - 1) s += ":";
  }
  s.toUpperCase();
  return s;
}

void setLeds(bool red, bool yellow) {
  digitalWrite(LED_RED,    red);
  digitalWrite(LED_YELLOW, yellow);
}

// non-blocking blink using millis — stepper keeps running throughout
void blinkRed(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_RED, HIGH);
    unsigned long t = millis();
    while (millis() - t < 150) stepper.run();
    digitalWrite(LED_RED, LOW);
    t = millis();
    while (millis() - t < 150) stepper.run();
  }
  digitalWrite(LED_RED, HIGH);   // back to closed state
}
