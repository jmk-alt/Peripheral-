#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#include <addons/TokenHelper.h>

// Configuration
#define WIFI_SSID "Juthi"
#define WIFI_PASSWORD "j12345678"
#define FIREBASE_API_KEY "AIzaSyAtnhx17OGQPTtEebmnmAgNkgiIi55d9vc"
#define FIREBASE_DATABASE_URL "smarthomesecurity-bd058-default-rtdb.firebaseio.com"
#define USER_EMAIL "smarthome@solvepanda.com"
#define USER_PASSWORD "12345678"

// Hardware Pins
#define DOOR_SENSOR_PIN     5   // GPIO5 (D1)
#define MOTION_SENSOR_PIN   4   // GPIO4 (D2)
#define BUZZER_PIN          0   // GPIO0 (D3)
#define LIGHT_PIN           14  // GPIO14 = D5

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

bool lastDoorState = false;
bool lastMotionState = false;
bool alertArmed = false;
bool lightState = false;
unsigned long lastDebounce = 0;
const unsigned long debounceDelay = 2000;

void logEvent(String message) {
  String timestamp = String(millis() / 1000.0, 2);
  Serial.println("[LOG " + timestamp + "s] " + message);
}

void hardwareTest() {
  logEvent("Starting hardware test...");
  digitalWrite(LIGHT_PIN, HIGH);
  delay(2000);
  digitalWrite(LIGHT_PIN, LOW);
  delay(2000);
  digitalWrite(LIGHT_PIN, lightState ? LOW : HIGH);  // ✅ LOW = ON, HIGH = OFF
  logEvent("Hardware test completed");
}

// Firebase Stream callbacks
void onStream(StreamData data) {
  String key = data.dataPath().substring(1);
  String value = data.stringData();
  logEvent("Stream update | Key: " + key + " | Value: " + value);

  if (key == "alarmArmed") {
    alertArmed = data.boolData();
    logEvent("Alarm state: " + String(alertArmed ? "ARMED" : "DISARMED"));
  } 
  else if (key == "lightStatus") {
    lightState = (value == "ON");
    digitalWrite(LIGHT_PIN, lightState ? LOW : HIGH);
    bool actualState = digitalRead(LIGHT_PIN);
    logEvent("Light command | Target: " + String(lightState ? "ON" : "OFF") + 
             " | Actual: " + String(actualState ? "ON" : "OFF"));
    if (actualState != lightState) {
      logEvent("State mismatch! Correcting...");
      digitalWrite(LIGHT_PIN, lightState ? LOW : HIGH);
    }
  }
}

void onTimeout(bool timeout) {
  if (timeout) logEvent("Stream timeout occurred");
}

void setup() {
  Serial.begin(115200);
  logEvent("System initialization started");

  pinMode(DOOR_SENSOR_PIN, INPUT_PULLUP);
  pinMode(MOTION_SENSOR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LIGHT_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LIGHT_PIN, LOW);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setAutoReconnect(true);
  logEvent("Connecting to WiFi...");
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (millis() - wifiStart > 30000) {
      logEvent("WiFi connection timeout. Restarting...");
      ESP.restart();
    }
  }
  logEvent("WiFi connected. IP: " + WiFi.localIP().toString());

  config.api_key = FIREBASE_API_KEY;
  config.database_url = FIREBASE_DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback;

  fbdo.setBSSLBufferSize(2048, 512);
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  logEvent("Firebase initialized");

  unsigned long authStart = millis();
  while (!Firebase.ready()) {
    delay(500);
    if (millis() - authStart > 30000) {
      logEvent("Firebase authentication timeout!");
      ESP.restart();
    }
  }
  logEvent("Authentication successful. UID: " + String(auth.token.uid.c_str()));

  hardwareTest();

  // Initial sync
  if (Firebase.getString(fbdo, "/system/lightStatus")) {
    lightState = fbdo.stringData() == "ON";
    digitalWrite(LIGHT_PIN, lightState ? LOW : HIGH);
  } else {
    logEvent("Light status fetch failed. Setting default OFF.");
    Firebase.setString(fbdo, "/system/lightStatus", "OFF");
    lightState = false;
    digitalWrite(LIGHT_PIN, LOW);
  }

  if (!Firebase.beginStream(fbdo, "/system")) {
    logEvent("Stream setup failed: " + fbdo.errorReason());
  }
  Firebase.setStreamCallback(fbdo, onStream, onTimeout);

  logEvent("Setup complete.");
}

void loop() {
  digitalWrite(LED_BUILTIN, Firebase.ready() ? LOW : HIGH);

  // Door state monitoring
  bool currentDoor = !digitalRead(DOOR_SENSOR_PIN);  // Pullup logic
  if (currentDoor != lastDoorState && (millis() - lastDebounce) > debounceDelay) {
    lastDebounce = millis();
    lastDoorState = currentDoor;
    String doorStatus = currentDoor ? "OPEN" : "CLOSED";
    if (Firebase.setString(fbdo, "/sensors/door", doorStatus)) {
      logEvent("Door state updated: " + doorStatus);
    } else {
      logEvent("Door update failed: " + fbdo.errorReason());
    }
  }

  // Motion detection logic
  bool currentMotion = digitalRead(MOTION_SENSOR_PIN);
  if (alertArmed && currentMotion && !lastMotionState) {
    logEvent("⚠️ Thief detected (motion + alarm armed)");
    digitalWrite(BUZZER_PIN, HIGH);
    if (Firebase.setBool(fbdo, "/alerts/motion", true)) {
      logEvent("Motion alert sent to Firebase");
    } else {
      logEvent("Motion alert failed: " + fbdo.errorReason());
    }
    delay(3000);  // Buzzer delay
    digitalWrite(BUZZER_PIN, LOW);
    Firebase.setBool(fbdo, "/alerts/motion", false);
    logEvent("Buzzer turned off and alert reset");
  }
  lastMotionState = currentMotion;

  // Auto reconnect check
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 5000) {
    lastCheck = millis();
    if (!Firebase.ready()) {
      logEvent("Firebase not ready. Reconnecting...");
      Firebase.begin(&config, &auth);
    }
    if (WiFi.status() != WL_CONNECTED) {
      logEvent("WiFi disconnected. Reconnecting...");
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }

  delay(100);
}
