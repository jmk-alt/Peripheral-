#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>

#define SS_PIN 10
#define RST_PIN 9
#define SERVO_PIN 6
#define PIR_PIN 2
#define MOTION_OUT_PIN 3
#define DOOR_STATUS_PIN 4

MFRC522 mfrc522(SS_PIN, RST_PIN);
Servo doorServo;

// Authorized UIDs
byte authorizedUID1[] = {0x4C, 0x93, 0x31, 0x03};
byte authorizedUID2[] = {0x0B, 0x49, 0xC0, 0x01};

bool doorUnlocked = false;
unsigned long unlockTime = 0;
const unsigned long unlockDuration = 5000; // 5 seconds

void setup() {
  Serial.begin(9600);
  SPI.begin();
  mfrc522.PCD_Init();

  doorServo.attach(SERVO_PIN);
  lockDoor(); // Start locked

  pinMode(PIR_PIN, INPUT);
  pinMode(MOTION_OUT_PIN, OUTPUT);
  pinMode(DOOR_STATUS_PIN, OUTPUT);

  digitalWrite(MOTION_OUT_PIN, LOW);
  digitalWrite(DOOR_STATUS_PIN, LOW);

  Serial.println("System Ready.");
}

void loop() {
  handlePIRSensor();
  handleRFID();
  handleDoorTimeout();
}

void handlePIRSensor() {
  if (digitalRead(PIR_PIN) == HIGH) {
    digitalWrite(MOTION_OUT_PIN, HIGH);
  } else {
    digitalWrite(MOTION_OUT_PIN, LOW);
  }
}

void handleRFID() {
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial())
    return;

  if (isAuthorized(mfrc522.uid.uidByte)) {
    unlockDoor();
  }

  mfrc522.PICC_HaltA();
}

bool isAuthorized(byte *uid) {
  return compareUID(uid, authorizedUID1) || compareUID(uid, authorizedUID2);
}

bool compareUID(byte *uid, byte *authorized) {
  for (byte i = 0; i < 4; i++) {
    if (uid[i] != authorized[i]) return false;
  }
  return true;
}

void unlockDoor() {
  doorServo.write(90); // Unlocked position
  digitalWrite(DOOR_STATUS_PIN, HIGH);
  doorUnlocked = true;
  unlockTime = millis();
  Serial.println("Door Unlocked");
}

void lockDoor() {
  doorServo.write(0); // Locked position
  digitalWrite(DOOR_STATUS_PIN, LOW);
  doorUnlocked = false;
  Serial.println("Door Locked");
}

void handleDoorTimeout() {
  if (doorUnlocked && (millis() - unlockTime >= unlockDuration)) {
    lockDoor();
  }
}
