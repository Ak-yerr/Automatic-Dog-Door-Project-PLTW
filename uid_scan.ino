/*
 * uid_scan.ino
 *
 * one-time use — upload this, open serial monitor at 9600 baud,
 * scan your dog's rfid tag, copy the uid bytes into dog_door.ino,
 * then re-upload dog_door.ino
 */

#include <SPI.h>
#include <MFRC522.h>

#define RFID_SS_PIN  10
#define RFID_RST_PIN  9

MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);

void setup() {
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("ready — hold tag near reader");
}

void loop() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  Serial.print("uid (hex): ");
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) Serial.print("0");
    Serial.print(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) Serial.print(":");
  }
  Serial.println();

  // also print in arduino array format — just paste this into dog_door.ino
  Serial.print("array format: { ");
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print("0x");
    if (rfid.uid.uidByte[i] < 0x10) Serial.print("0");
    Serial.print(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) Serial.print(", ");
  }
  Serial.println(" }");
  Serial.println("---");

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  delay(1000);
}
