// Blynk template ID / name / auth token and Wi-Fi live in secrets.h (see secrets.example.h).
// They must be defined before BlynkSimpleEsp32.h is included.
#include "secrets.h"

#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LiquidCrystal_I2C.h>

// ===== WiFi =====
char ssid[] = SECRET_WIFI_SSID;
char pass[] = SECRET_WIFI_PASS;

// ===== Pins =====
const int trigPin = 13;
const int echoPin = 14;
const int ledGreenPin = 2;
const int ledRedPin = 5;
const int buzzerPin = 4;
const int relayPin = 18;

// ===== Buzzer PWM =====
const int buzzerFreq = 5000;
const int buzzerResolution = 8;
const int buzzerVolume = 20;

// ===== Displays =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
LiquidCrystal_I2C lcd(0x27, 16, 2);

BlynkTimer timer;

// ===== State =====
float distanceCm = 0;
float safeDistance = 20.0;
bool tooClose = false;
bool alarmEnabled = true;   // V0: phone switch for the buzzer

// ---------- Read distance ----------
float readDistanceCm() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) return -1;
  return duration * 0.0343 / 2.0;
}

// ---------- OLED ----------
void updateOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Distance: " + String(distanceCm, 1) + " cm");
  display.println(tooClose ? "Status: TOO CLOSE!" : "Status: SAFE");
  display.println(alarmEnabled ? "Alarm: ON" : "Alarm: OFF");

  int barMax = 100;
  float d = (distanceCm < 0) ? barMax : distanceCm;
  if (d > barMax) d = barMax;
  int filled = map((int)(barMax - d), 0, barMax, 0, SCREEN_WIDTH - 4);
  display.drawRect(0, 44, SCREEN_WIDTH, 16, WHITE);
  display.fillRect(2, 46, filled, 12, WHITE);
  display.display();
}

// ---------- LCD ----------
void updateLCD() {
  lcd.setCursor(0, 0);
  lcd.print("Dist: " + String(distanceCm, 1) + "cm   ");
  lcd.setCursor(0, 1);
  lcd.print(tooClose ? "!! TOO CLOSE !! " : "Status: SAFE    ");
}

// ---------- Alerts ----------
void updateAlerts() {
  if (tooClose) {
    digitalWrite(ledRedPin, HIGH);
    digitalWrite(ledGreenPin, LOW);
    if (alarmEnabled) {
      ledcWriteTone(buzzerPin, 1000);
      ledcWrite(buzzerPin, buzzerVolume);
    } else {
      ledcWrite(buzzerPin, 0);
    }
  } else {
    digitalWrite(ledRedPin, LOW);
    digitalWrite(ledGreenPin, HIGH);
    ledcWrite(buzzerPin, 0);
  }
}

// ---------- Runs every 1 second: measure + update + send to Blynk ----------
void sensorTask() {
  float d = readDistanceCm();
  if (d > 0) distanceCm = d;

  tooClose = (distanceCm > 0 && distanceCm < safeDistance);

  updateOLED();
  updateLCD();
  updateAlerts();

  Blynk.virtualWrite(V1, distanceCm);   // send distance to phone

  Serial.print("Distance: ");
  Serial.print(distanceCm);
  Serial.println(tooClose ? " cm | TOO CLOSE" : " cm | SAFE");
}

// ---------- Phone -> ESP32 ----------
BLYNK_WRITE(V0) {            // Alarm switch
  alarmEnabled = param.asInt();
}

BLYNK_WRITE(V2) {            // Relay switch
  digitalWrite(relayPin, param.asInt());
}

// Sync switch states when the device (re)connects
BLYNK_CONNECTED() {
  Blynk.syncVirtual(V0, V2);
}

void setup() {
  Serial.begin(115200);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(ledGreenPin, OUTPUT);
  pinMode(ledRedPin, OUTPUT);
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);
  ledcAttach(buzzerPin, buzzerFreq, buzzerResolution);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearDisplay();
  display.display();

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Connecting Blynk");

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Blynk connected");
  delay(1500);

  timer.setInterval(1000L, sensorTask);
}

void loop() {
  Blynk.run();
  timer.run();
}