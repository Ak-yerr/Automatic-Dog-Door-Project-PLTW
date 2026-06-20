#include <SPI.h>
#include <MFRC522.h>
#include <AccelStepper.h>

// pins
#define PIR_PIN        2
#define STEP_PIN       3
#define DIR_PIN        4
#define LED_RED        5
#define LED_YELLOW     A3
#define RFID_SS_PIN    10
#define RFID_RST_PIN   9

// door travel in steps (tune to your gearing)
#define DOOR_STEPS     200

// timing (ms)
#define RFID_WINDOW    10000UL   // how long to wait for tag after motion
#define DOOR_HOLD      5000UL    // minimum time door stays open
#define CLOSE_DELAY    2000UL    // extra wait if pir still active when closing

AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);

// --- authorized tag UIDs --- add/remove as needed
const byte AUTHORIZED_TAGS[][4] = {
  { 0xDE, 0xAD, 0xBE, 0xEF },   // replace with your pet's tag UID
};
const int TAG_COUNT = sizeof(AUTHORIZED_TAGS) / sizeof(AUTHORIZED_TAGS[0]);

enum State { IDLE, WAITING_FOR_RFID, DOOR_OPEN, CLOSING };
State state = IDLE;

unsigned long stateEnteredAt = 0;

void setup() {
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();

  pinMode(PIR_PIN,    INPUT);
  pinMode(LED_RED,    OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);

  stepper.setMaxSpeed(500);
  stepper.setAcceleration(200);

  // start closed, red on
  setLeds(HIGH, LOW);
  Serial.println("ready");
}

void loop() {
  stepper.run();   // must be called every iteration for accelstepper to work

  switch (state) {

    case IDLE:
      if (digitalRead(PIR_PIN) == HIGH) {
        Serial.println("motion — waiting for rfid");
        state = WAITING_FOR_RFID;
        stateEnteredAt = millis();
      }
      break;

    case WAITING_FOR_RFID:
      if (millis() - stateEnteredAt > RFID_WINDOW) {
        // timed out — nobody showed a tag
        Serial.println("rfid timeout");
        state = IDLE;
        break;
      }
      if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
        if (isAuthorized(rfid.uid.uidByte, rfid.uid.size)) {
          Serial.print("authorized — door opening @ ");
          Serial.println(millis());
          openDoor();
          state = DOOR_OPEN;
          stateEnteredAt = millis();
        } else {
          Serial.println("denied");
          blinkRed(3);
        }
        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();
      }
      break;

    case DOOR_OPEN:
      if (millis() - stateEnteredAt >= DOOR_HOLD) {
        state = CLOSING;
        stateEnteredAt = millis();
      }
      break;

    case CLOSING:
      if (digitalRead(PIR_PIN) == HIGH) {
        // pet still in doorway — reset hold timer
        stateEnteredAt = millis() + CLOSE_DELAY;
        break;
      }
      if (stepper.distanceToGo() == 0) {
        closeDoor();
      }
      if (stepper.distanceToGo() == 0) {
        // fully closed
        setLeds(HIGH, LOW);
        Serial.println("door closed");
        state = IDLE;
      }
      break;
  }
}

void openDoor() {
  setLeds(LOW, HIGH);   // yellow = open
  stepper.move(DOOR_STEPS);
}

void closeDoor() {
  stepper.move(-DOOR_STEPS);
}

bool isAuthorized(byte *uid, byte uidSize) {
  if (uidSize != 4) return false;
  for (int i = 0; i < TAG_COUNT; i++) {
    if (memcmp(uid, AUTHORIZED_TAGS[i], 4) == 0) return true;
  }
  return false;
}

void setLeds(bool red, bool yellow) {
  digitalWrite(LED_RED,    red);
  digitalWrite(LED_YELLOW, yellow);
}

void blinkRed(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_RED, HIGH);
    delay(150);
    digitalWrite(LED_RED, LOW);
    delay(150);
  }
  digitalWrite(LED_RED, HIGH);   // back to closed state
}
