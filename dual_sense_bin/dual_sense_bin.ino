// Blynk template ID / name / auth token and Wi-Fi live in secrets.h (see secrets.example.h).
// They must be defined before BlynkSimpleEsp32.h is included.
#include "secrets.h"

#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "HX711.h"

// ===== WiFi =====
char ssid[] = SECRET_WIFI_SSID;
char pass[] = SECRET_WIFI_PASS;

// ===== Pins =====
#define LOADCELL_DOUT_PIN 23
#define LOADCELL_SCK_PIN 19
const int trigPin = 13;
const int echoPin = 14;
const int ledGreenPin = 2;
const int ledRedPin = 5;
const int buzzerPin = 4;

// TFT 2.0" ST7789 (SPI): VCC->3.3V, GND->GND
#define TFT_SCK  18   // screen pin SCL
#define TFT_MOSI 25   // screen pin SDA
#define TFT_CS   26
#define TFT_DC   27
#define TFT_RST  33

// ===== Buzzer PWM =====
const int buzzerFreq = 5000;
const int buzzerResolution = 8;
const int buzzerVolume = 20;

Adafruit_ST7789 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
HX711 scale;
BlynkTimer timer;

// ===== Settings =====
float calibrationFactor = 501.7;   // from your S24 Ultra calibration
float capacityGrams = 1000.0;      // weight that counts as 100% (adjust from phone)
const int warnPercent = 80;        // "high" threshold for both metrics
const float minDistanceCm = 5.0;   // sonar distance that counts as 100% volume

// ===== State =====
float emptyDistanceCm = 30.0;      // measured at startup with an EMPTY bin
float weightG = 0;
float distanceCm = 0;
int weightPct = 0;
int volumePct = 0;
bool alarmEnabled = true;          // V0

enum BinState { BIN_OK, HEAVY, BULKY, FULL };
BinState state = BIN_OK;
BinState lastNotified = BIN_OK;
String stateText = "OK";

// =====================================================================
//                               TFT
// =====================================================================
const uint16_t C_BLACK  = 0x0000;
const uint16_t C_WHITE  = 0xFFFF;
const uint16_t C_GREEN  = 0x07E0;
const uint16_t C_RED    = 0xF800;
const uint16_t C_YELLOW = 0xFFE0;
const uint16_t C_ORANGE = 0xFD20;
const uint16_t C_GREY   = 0x7BEF;

// bin drawing (left side)
const int BIN_X = 24, BIN_Y = 96, BIN_W = 96, BIN_H = 116;
const int IN_X = BIN_X + 2, IN_Y = BIN_Y + 2, IN_W = BIN_W - 4, IN_H = BIN_H - 4;

// remembered values so only changed parts are redrawn (no flicker)
int shownState = -1, shownOnline = -1, shownInv = -1;
int shownBinCol = -1, shownBinH = -1, shownVolPct = -1, shownWPct = -1;
String shownWeight = "", shownCap = "", shownDist = "", shownAlarm = "";
bool flashPhase = false;

uint16_t stateColor(BinState s) {
  switch (s) {
    case HEAVY: return C_ORANGE;
    case BULKY: return C_YELLOW;
    case FULL:  return C_RED;
    default:    return C_GREEN;
  }
}

uint16_t levelColor(int pct) {
  if (pct >= warnPercent) return C_RED;
  if (pct >= 60) return C_YELLOW;
  return C_GREEN;
}

String padRight(String s, int n) {
  while ((int)s.length() < n) s += ' ';
  return s;
}

void showMessage(const char* l1, const char* l2) {
  tft.fillScreen(C_BLACK);
  tft.setTextColor(C_WHITE, C_BLACK);
  tft.setTextSize(2);
  tft.setCursor(20, 90);  tft.print(l1);
  tft.setCursor(20, 120); tft.print(l2);
}

void drawStatic() {
  tft.fillScreen(C_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(C_GREY, C_BLACK);
  tft.setCursor(BIN_X, 62); tft.print("VOLUME");
  tft.setCursor(156, 62);   tft.print("WEIGHT");
  tft.drawRect(BIN_X - 6, BIN_Y - 8, BIN_W + 12, 8, C_WHITE);   // lid
  tft.drawRect(BIN_X, BIN_Y, BIN_W, BIN_H, C_WHITE);            // bin body
  tft.drawRect(156, 134, 150, 22, C_WHITE);                     // weight bar frame

  shownState = shownOnline = shownInv = -1;
  shownBinCol = shownBinH = shownVolPct = shownWPct = -1;
  shownWeight = shownCap = shownDist = shownAlarm = "";
}

void drawBanner(bool inv) {
  uint16_t bg = stateColor(state);
  uint16_t fg = (state == FULL) ? C_WHITE : C_BLACK;
  if (inv) { bg = C_WHITE; fg = C_RED; }       // FULL: flashing banner

  tft.fillRect(0, 0, 320, 52, bg);
  tft.setTextColor(fg, bg);
  tft.setTextSize(4);
  tft.setCursor(12, 10);
  tft.print(stateText);
  tft.setTextSize(2);
  tft.setCursor(206, 18);
  tft.print(Blynk.connected() ? "ONLINE" : "OFFLINE");
}

void updateTFT() {
  // ---- banner (state colour + Blynk status) ----
  bool online = Blynk.connected();
  flashPhase = !flashPhase;
  bool inv = (state == FULL) && flashPhase;
  if ((int)state != shownState || (int)online != shownOnline || (int)inv != shownInv) {
    shownState = (int)state;
    shownOnline = (int)online;
    shownInv = (int)inv;
    drawBanner(inv);
  }

  // ---- bin: trash level = volume % ----
  int h = map(volumePct, 0, 100, 0, IN_H);
  uint16_t vcol = levelColor(volumePct);
  if ((int)vcol != shownBinCol) {
    tft.fillRect(IN_X, IN_Y, IN_W, IN_H, C_BLACK);
    tft.fillRect(IN_X, IN_Y + IN_H - h, IN_W, h, vcol);
    shownBinCol = (int)vcol;
    shownBinH = h;
  } else if (h > shownBinH) {
    tft.fillRect(IN_X, IN_Y + IN_H - h, IN_W, h - shownBinH, vcol);
    shownBinH = h;
  } else if (h < shownBinH) {
    tft.fillRect(IN_X, IN_Y + IN_H - shownBinH, IN_W, shownBinH - h, C_BLACK);
    shownBinH = h;
  }
  if (volumePct != shownVolPct) {
    shownVolPct = volumePct;
    tft.setTextSize(2);
    tft.setTextColor(vcol, C_BLACK);
    tft.setCursor(BIN_X, 218);
    tft.print(padRight(String(volumePct) + "%", 5));
  }

  // ---- weight: big grams number + bar ----
  String wTxt = padRight(String((int)weightG) + "g", 6);
  if (wTxt != shownWeight) {
    shownWeight = wTxt;
    tft.setTextSize(4);
    tft.setTextColor(C_WHITE, C_BLACK);
    tft.setCursor(156, 88);
    tft.print(wTxt);
  }
  if (weightPct != shownWPct) {
    shownWPct = weightPct;
    int w = map(weightPct, 0, 100, 0, 146);
    tft.fillRect(158, 136, 146, 18, C_BLACK);
    tft.fillRect(158, 136, w, 18, levelColor(weightPct));
  }
  String capTxt = padRight(String(weightPct) + "% of " + String((int)capacityGrams) + "g", 13);
  if (capTxt != shownCap) {
    shownCap = capTxt;
    tft.setTextSize(2);
    tft.setTextColor(C_GREY, C_BLACK);
    tft.setCursor(156, 164);
    tft.print(capTxt);
  }

  // ---- distance + alarm switch ----
  String dTxt = padRight("DIST " + (distanceCm > 0 ? String(distanceCm, 1) : String("---")) + "cm", 13);
  if (dTxt != shownDist) {
    shownDist = dTxt;
    tft.setTextSize(2);
    tft.setTextColor(C_GREY, C_BLACK);
    tft.setCursor(156, 192);
    tft.print(dTxt);
  }
  String aTxt = padRight(alarmEnabled ? "ALARM ON" : "ALARM OFF", 10);
  if (aTxt != shownAlarm) {
    shownAlarm = aTxt;
    tft.setTextSize(2);
    tft.setTextColor(alarmEnabled ? C_GREEN : C_GREY, C_BLACK);
    tft.setCursor(156, 214);
    tft.print(aTxt);
  }
}

// =====================================================================
//                              Sensors
// =====================================================================

// ---------- Sonar: one reading ----------
float readDistanceOnce() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) return -1;
  return duration * 0.0343 / 2.0;
}

// ---------- Sonar: median of 5 (garbage surface is uneven/noisy) ----------
float readDistanceMedian() {
  float s[5];
  int n = 0;
  for (int i = 0; i < 5; i++) {
    float d = readDistanceOnce();
    if (d > 0) s[n++] = d;
    delay(30);
  }
  if (n == 0) return -1;
  for (int i = 1; i < n; i++) {            // simple insertion sort
    float key = s[i];
    int j = i - 1;
    while (j >= 0 && s[j] > key) { s[j + 1] = s[j]; j--; }
    s[j + 1] = key;
  }
  return s[n / 2];
}

// ---------- Decide state from the two metrics ----------
void evaluateState() {
  bool weightHigh = weightPct >= warnPercent;
  bool volumeHigh = volumePct >= warnPercent;

  if (weightHigh && volumeHigh) { state = FULL;   stateText = "FULL"; }
  else if (weightHigh)          { state = HEAVY;  stateText = "HEAVY"; }
  else if (volumeHigh)          { state = BULKY;  stateText = "BULKY"; }
  else                          { state = BIN_OK; stateText = "OK"; }
}

// ---------- LEDs, buzzer, phone notifications ----------
void updateAlerts() {
  if (state == BIN_OK) {
    digitalWrite(ledRedPin, LOW);
    digitalWrite(ledGreenPin, HIGH);
    ledcWrite(buzzerPin, 0);
  } else {
    digitalWrite(ledRedPin, HIGH);
    digitalWrite(ledGreenPin, LOW);
    if (alarmEnabled) {
      int tone_ = (state == FULL) ? 1000 : (state == HEAVY ? 500 : 1500);
      ledcWriteTone(buzzerPin, tone_);
      ledcWrite(buzzerPin, buzzerVolume);
    } else {
      ledcWrite(buzzerPin, 0);
    }
  }

  // Send one notification each time the state changes into a problem state
  if (state != lastNotified) {
    if (state == FULL)  Blynk.logEvent("bin_full",  "Bin is full - time to empty it!");
    if (state == HEAVY) Blynk.logEvent("bin_heavy", "Bin is heavy - lift with care.");
    if (state == BULKY) Blynk.logEvent("bin_bulky", "Bin is out of space but light - flatten the trash.");
    lastNotified = state;
  }
}

// ---------- Runs every 1 second ----------
void sensorTask() {
  // Weight metric (skipped if the HX711 is not ready, so nothing can freeze)
  if (scale.is_ready()) {
    weightG = scale.get_units(5);
    if (weightG < 0) weightG = 0;
  }
  weightPct = constrain((int)(weightG / capacityGrams * 100.0), 0, 100);

  // Volume metric (sonar looks down at the trash surface)
  float d = readDistanceMedian();
  if (d > 0) distanceCm = d;
  float range = emptyDistanceCm - minDistanceCm;
  if (range > 1.0 && distanceCm > 0) {
    volumePct = constrain((int)((emptyDistanceCm - distanceCm) / range * 100.0), 0, 100);
  } else {
    volumePct = 0;
  }

  evaluateState();
  updateTFT();
  updateAlerts();

  Blynk.virtualWrite(V1, weightG);
  Blynk.virtualWrite(V2, weightPct);
  Blynk.virtualWrite(V5, volumePct);
  Blynk.virtualWrite(V6, stateText);

  Serial.print("W: ");
  Serial.print(weightG);
  Serial.print("g (");
  Serial.print(weightPct);
  Serial.print("%) | Dist: ");
  Serial.print(distanceCm);
  Serial.print("cm V: ");
  Serial.print(volumePct);
  Serial.print("% | ");
  Serial.println(stateText);
}

// ---------- Re-zero both sensors (bin must be empty) ----------
void calibrateEmpty() {
  if (scale.wait_ready_timeout(2000)) scale.tare();
  else Serial.println("HX711 not ready - skipping tare");

  float d = readDistanceMedian();
  if (d > 0) {
    emptyDistanceCm = d;
  } else {
    Serial.println("Sonar failed during calibration, keeping old empty distance");
  }
  lastNotified = BIN_OK;
  Serial.print("Empty distance set to ");
  Serial.print(emptyDistanceCm);
  Serial.println(" cm");
}

// ---------- Phone -> ESP32 ----------
BLYNK_WRITE(V0) {            // Alarm switch
  alarmEnabled = param.asInt();
}

BLYNK_WRITE(V3) {            // Capacity slider (grams)
  capacityGrams = param.asInt();
}

BLYNK_WRITE(V4) {            // Reset button: call after emptying the bin
  if (param.asInt() == 1) {
    calibrateEmpty();
  }
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(V0, V3);
}

void setup() {
  Serial.begin(115200);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(ledGreenPin, OUTPUT);
  pinMode(ledRedPin, OUTPUT);
  ledcAttach(buzzerPin, buzzerFreq, buzzerResolution);

  // TFT (hardware SPI on custom pins)
  SPI.begin(TFT_SCK, -1, TFT_MOSI, -1);
  tft.init(240, 320);
  tft.setSPISpeed(27000000);
  tft.setRotation(1);                 // landscape 320x240
  showMessage("Keep bin EMPTY", "Calibrating...");

  // Bin must be EMPTY and still when powering on
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibrationFactor);
  delay(1500);
  calibrateEmpty();

  showMessage("Connecting", "Blynk...");
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  drawStatic();

  timer.setInterval(1000L, sensorTask);
}

void loop() {
  Blynk.run();
  timer.run();
}