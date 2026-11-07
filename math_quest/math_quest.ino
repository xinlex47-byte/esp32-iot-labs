// =====================================================
//  MATH QUEST - Game Boy style math adventure (ESP32-S3 + ThingsBoard)
// =====================================================
//  Stages:
//    Easy   : + 1-4 -> + 1-7 -> + 1-9 -> - 1-4 -> - 1-7 -> - 1-9
//    Medium : x 1-4 -> x 1-7 -> x 1-9
//    Hard   : BOSS (mixed + - x /, plus "missing number" questions like 5 + ? = 12)
//  Clear a stage with 3 correct answers (BOSS needs 5). Progress is auto-saved.
//
//  Controls: D-pad to move, A to confirm, B to go back
//  Cheat code: on the title screen press UP UP DOWN DOWN LEFT RIGHT LEFT RIGHT B A = unlock all
//
//  Libraries: PubSubClient, ArduinoJson, Adafruit GFX,
//             Adafruit ST7735 and ST7789, Adafruit SSD1306
//
//  ThingsBoard RPC commands:
//    start / pause / reset          start (resume) / pause / back to title screen
//    setDifficulty "easy"           easy / medium / hard (remote unlock + jump to stage)
//    setQuestions 3                 correct answers needed per stage (1-20)
//    setTimeLimit 10                seconds per question (0 = no limit)
//    answer "B"                     answer from the dashboard (A top-left, B top-right, C bottom-left, D bottom-right)
//    unlockAll / resetProgress      unlock all stages / clear saved progress
// =====================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_SSD1306.h>

// ---------- Network settings ----------
#include "secrets.h"   // Wi-Fi + ThingsBoard token live here (see secrets.example.h)
const char* WIFI_SSID = SECRET_WIFI_SSID;
const char* WIFI_PASS = SECRET_WIFI_PASS;
const char* TB_HOST   = SECRET_TB_HOST;
const char* TB_TOKEN  = SECRET_TB_TOKEN;
// --------------------------------------

// ---------- Pins ----------
#define TFT_CS    42
#define TFT_DC    41
#define TFT_RST   40
#define TFT_MOSI  39
#define TFT_SCK   38
#define OLED_SDA  10
#define OLED_SCL  11
#define LED_GREEN 15
#define LED_RED   16
#define BUZZER    17
#define RELAY     18
#define USE_RELAY false
#define SERVO_PIN    12   // SG90 signal (orange wire)
#define CHEST_CLOSED 0    // servo angle when the chest is closed
#define CHEST_OPEN   90   // servo angle when the chest is open
#define OLED_ROTATION 3   // OLED orientation: 3 = portrait (64 wide x 128 tall)

enum Btn { B_UP, B_DOWN, B_LEFT, B_RIGHT, BTN_A, BTN_B };
const int BTN_PIN[6] = {4, 5, 6, 7, 8, 9};   // up, down, left, right, A, B

// ---------- Game Boy 4-shade green palette ----------
#define GB0 0x09C1   // darkest
#define GB1 0x3306
#define GB2 0x8D61
#define GB3 0x9DE1   // lightest (background)

// ---------- Stages ----------
struct Stage { char op; int maxN; const char* name; };
const Stage STAGES[] = {
  {'+', 4, "+  1-4"}, {'+', 7, "+  1-7"}, {'+', 9, "+  1-9"},
  {'-', 4, "-  1-4"}, {'-', 7, "-  1-7"}, {'-', 9, "-  1-9"},
  {'x', 4, "x  1-4"}, {'x', 7, "x  1-7"}, {'x', 9, "x  1-9"},
  {'M', 9, "+ - x /  MIX"}
};
const int BOSS = 9;
const char* DIFF_NAME[3] = {"Easy", "Medium", "Hard"};

// ---------- Objects ----------
Adafruit_ST7789  tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
WiFiClient   wifi;
PubSubClient mqtt(wifi);
Preferences  prefs;
bool oledOK = false;

// ---------- Game state ----------
enum State { TITLE, SELECT, ASKING, FEEDBACK, CLEAR, PAUSED };
State state = TITLE, stateBeforePause = TITLE;
const char* STATE_NAME[] = {"title", "select", "playing", "feedback", "stage_clear", "paused"};

int unlocked = 0;          // highest unlocked stage (0-9), saved to flash
int selStage = 0, curStage = 0;
int needCorrect = 3;       // correct answers needed to clear a stage
int timeLimit = 0;
int menuIdx = 0, cursor = 0;
int stageCorrect = 0;
int questionNo = 0, score = 0, correctCount = 0, wrongCount = 0, streak = 0, wrongStreak = 0;
String questionText, fullEquation, clearMsg1, clearMsg2;
int correctAnswer, correctIndex;
int options[4];
bool lastCorrect = false;
unsigned long questionStart = 0, pausedAt = 0, feedbackUntil = 0;
unsigned long relayOffAt = 0, lastBarDraw = 0, lastConnTry = 0;

bool btnLast[6] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
unsigned long btnChange[6] = {0, 0, 0, 0, 0, 0};

const int KONAMI[10] = {B_UP, B_UP, B_DOWN, B_DOWN, B_LEFT, B_RIGHT, B_LEFT, B_RIGHT, BTN_B, BTN_A};
int konamiPos = 0;

// =====================================================
//  Helpers
// =====================================================
int diffOf(int st)  { return st <= 5 ? 0 : (st <= 8 ? 1 : 2); }
int goal()          { return curStage == BOSS ? needCorrect + 2 : needCorrect; }
void saveProgress() { prefs.putInt("unlocked", unlocked); }

String weapons() {
  String w = "+";
  if (unlocked >= 3) w += " -";
  if (unlocked >= 6) w += " x";
  if (unlocked >= 9) w += " /";
  return w;
}

String stageLabel(int st) { return st == BOSS ? String("BOSS") : String("STAGE ") + (st + 1); }

void tftCenter(const String& s, int y, uint8_t size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color);
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((320 - w) / 2, y);
  tft.print(s);
}

void gbScreen(uint16_t bg, uint16_t border) {
  tft.fillScreen(bg);
  tft.drawRect(4, 4, 312, 232, border);
  tft.drawRect(6, 6, 308, 228, border);
}

void drawHeart(int x, int y, int s, uint16_t c) {
  tft.fillCircle(x + s / 4, y + s / 4, s / 4, c);
  tft.fillCircle(x + 3 * s / 4, y + s / 4, s / 4, c);
  tft.fillTriangle(x, y + s / 3, x + s, y + s / 3, x + s / 2, y + s, c);
}

void drawHearts(int x, int y, uint16_t full, uint16_t empty) {
  int left = max(0, 3 - wrongStreak);
  for (int i = 0; i < 3; i++) drawHeart(x + i * 24, y, 18, i < left ? full : empty);
}

// =====================================================
//  OLED: character status screen (calculator robot vs question-mark monster)
//  Shows only character status: HP, LV, EXP, expression and weapon.
//  Score = EXP (1 level per 10 points). HP = 3 - wrong streak, refilled on a correct answer.
// =====================================================
enum HeroMood { H_IDLE, H_HIT, H_HURT, H_WIN, H_SLEEP };
int heroMood = H_IDLE;
int heroFrame = 0;
unsigned long heroSince = 0, lastOledTick = 0;
String heroCap1 = "", heroCap2 = "";

int heroHP() { return max(0, 3 - wrongStreak); }
int heroLV() { return score / 10 + 1; }

char heroWeapon() {                               // strongest unlocked operator
  if (unlocked >= 9) return '/';
  if (unlocked >= 6) return 'x';
  if (unlocked >= 3) return '-';
  return '+';
}

void oledCenter(const String& s, int y, int size) {
  oled.setTextSize(size);
  oled.setCursor((64 - (int)s.length() * 6 * size) / 2, y);
  oled.print(s);
}

void oledHeart(int x, int y, bool full) {         // 9x8 heart
  if (full) {
    oled.fillCircle(x + 2, y + 2, 2, SSD1306_WHITE);
    oled.fillCircle(x + 6, y + 2, 2, SSD1306_WHITE);
    oled.fillTriangle(x, y + 3, x + 8, y + 3, x + 4, y + 7, SSD1306_WHITE);
  } else {
    oled.drawCircle(x + 2, y + 2, 2, SSD1306_WHITE);
    oled.drawCircle(x + 6, y + 2, 2, SSD1306_WHITE);
    oled.drawLine(x, y + 3, x + 4, y + 7, SSD1306_WHITE);
    oled.drawLine(x + 8, y + 3, x + 4, y + 7, SSD1306_WHITE);
  }
}

void drawWeapon(char w, int x, int y) {           // 7x7 operator weapon
  if (w == '+') {
    oled.fillRect(x + 2, y, 3, 7, SSD1306_WHITE);
    oled.fillRect(x, y + 2, 7, 3, SSD1306_WHITE);
  } else if (w == '-') {
    oled.fillRect(x, y + 2, 7, 3, SSD1306_WHITE);
  } else if (w == 'x') {
    for (int i = 0; i < 2; i++) {
      oled.drawLine(x + i, y, x + 6, y + 6 - i, SSD1306_WHITE);
      oled.drawLine(x, y + i, x + 6 - i, y + 6, SSD1306_WHITE);
      oled.drawLine(x + 6 - i, y, x, y + 6 - i, SSD1306_WHITE);
      oled.drawLine(x + 6, y + i, x + i, y + 6, SSD1306_WHITE);
    }
  } else {                                        // division sign
    oled.fillRect(x, y + 3, 7, 2, SSD1306_WHITE);
    oled.fillRect(x + 2, y, 3, 2, SSD1306_WHITE);
    oled.fillRect(x + 2, y + 6, 3, 2, SSD1306_WHITE);
  }
}

// face: 0 normal, 1 blink, 2 happy, 3 hurt, 4 sleeping
void drawBot(int cx, int y, int face) {
  oled.drawLine(cx, y - 5, cx, y - 1, SSD1306_WHITE);           // antenna
  oled.fillCircle(cx, y - 6, 1, SSD1306_WHITE);
  oled.fillRoundRect(cx - 11, y, 22, 32, 3, SSD1306_WHITE);     // body
  oled.fillRect(cx - 8, y + 3, 16, 10, SSD1306_BLACK);          // LCD screen (face)

  if (face == 0) {
    oled.fillRect(cx - 5, y + 5, 2, 3, SSD1306_WHITE);
    oled.fillRect(cx + 3, y + 5, 2, 3, SSD1306_WHITE);
    oled.fillRect(cx - 2, y + 10, 4, 1, SSD1306_WHITE);
  } else if (face == 1 || face == 4) {
    oled.fillRect(cx - 6, y + 7, 3, 1, SSD1306_WHITE);
    oled.fillRect(cx + 3, y + 7, 3, 1, SSD1306_WHITE);
    if (face == 1) oled.fillRect(cx - 2, y + 10, 4, 1, SSD1306_WHITE);
  } else if (face == 2) {                                        // ^ ^ eyes + smile
    oled.drawLine(cx - 6, y + 7, cx - 5, y + 5, SSD1306_WHITE);
    oled.drawLine(cx - 5, y + 5, cx - 4, y + 7, SSD1306_WHITE);
    oled.drawLine(cx + 3, y + 7, cx + 4, y + 5, SSD1306_WHITE);
    oled.drawLine(cx + 4, y + 5, cx + 5, y + 7, SSD1306_WHITE);
    oled.drawLine(cx - 3, y + 10, cx + 3, y + 10, SSD1306_WHITE);
    oled.drawPixel(cx - 4, y + 9, SSD1306_WHITE);
    oled.drawPixel(cx + 4, y + 9, SSD1306_WHITE);
  } else {                                                       // X X eyes + wavy mouth
    oled.drawLine(cx - 6, y + 4, cx - 3, y + 7, SSD1306_WHITE);
    oled.drawLine(cx - 3, y + 4, cx - 6, y + 7, SSD1306_WHITE);
    oled.drawLine(cx + 3, y + 4, cx + 6, y + 7, SSD1306_WHITE);
    oled.drawLine(cx + 6, y + 4, cx + 3, y + 7, SSD1306_WHITE);
    oled.drawLine(cx - 3, y + 11, cx - 1, y + 10, SSD1306_WHITE);
    oled.drawLine(cx - 1, y + 10, cx + 1, y + 11, SSD1306_WHITE);
    oled.drawLine(cx + 1, y + 11, cx + 3, y + 10, SSD1306_WHITE);
  }

  for (int r = 0; r < 3; r++)                                    // calculator keypad
    for (int c = 0; c < 3; c++)
      oled.fillRect(cx - 7 + c * 5, y + 17 + r * 5, 3, 3, SSD1306_BLACK);

  oled.fillRect(cx - 8, y + 32, 5, 4, SSD1306_WHITE);            // feet
  oled.fillRect(cx + 3, y + 32, 5, 4, SSD1306_WHITE);
  oled.drawLine(cx + 11, y + 18, cx + 13, y + 16, SSD1306_WHITE); // arm holding the weapon
}

void drawBotKO(int cx, int y) {                    // knocked out: lying down, screen shows ERR
  oled.fillRoundRect(cx - 16, y + 18, 32, 17, 3, SSD1306_WHITE);
  oled.fillRect(cx - 12, y + 21, 22, 11, SSD1306_BLACK);
  oled.setTextSize(1);
  oled.setCursor(cx - 10, y + 23);
  oled.print("ERR");
  oled.drawLine(0, y + 36, 63, y + 36, SSD1306_WHITE);           // ground
}

void drawEnemy(int x, int y, bool popped) {        // "?" monster = the unsolved question
  if (popped) {                                    // burst when defeated
    const int dx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    const int dy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    for (int i = 0; i < 8; i++)
      oled.drawLine(x + dx[i] * 3, y + dy[i] * 3, x + dx[i] * 8, y + dy[i] * 8, SSD1306_WHITE);
    return;
  }
  oled.fillCircle(x, y, 7, SSD1306_WHITE);
  oled.fillRect(x - 5, y + 6, 3, 2, SSD1306_WHITE);
  oled.fillRect(x + 2, y + 6, 3, 2, SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK);
  oled.setTextSize(1);
  oled.setCursor(x - 2, y - 3);
  oled.print('?');
  oled.setTextColor(SSD1306_WHITE);
}

void oledDraw() {
  if (!oledOK) return;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextWrap(false);

  // 1. Status bar: HP + LV + EXP bar
  for (int i = 0; i < 3; i++) oledHeart(i * 10, 1, i < heroHP());
  oled.setTextSize(1);
  oled.setCursor(40, 1);
  oled.print(String("LV") + heroLV());
  oled.setCursor(0, 12);
  oled.print("EXP");
  oled.drawRect(20, 12, 44, 7, SSD1306_WHITE);
  oled.fillRect(22, 14, (score % 10) * 40 / 10, 3, SSD1306_WHITE);

  // 2. Battle scene
  unsigned long t = millis() - heroSince;
  int bob = (heroFrame / 3) % 2;
  const int by = 40;
  char w = heroWeapon();
  bool ko = (heroHP() == 0) && heroMood != H_HIT && heroMood != H_WIN;

  if (ko) {
    drawBotKO(24, by);
  } else if (heroMood == H_SLEEP) {
    drawBot(21, by, 4);
    oled.setCursor(36, 34 - (heroFrame % 4) * 2); oled.print('z');
    oled.setCursor(44, 26 - (heroFrame % 4) * 2); oled.print('Z');
  } else if (heroMood == H_HIT) {                  // throw the weapon at the "?" monster
    drawBot(21, by, 2);
    if (t < 600) {
      drawWeapon(w, 34 + (int)(t * 14 / 600), by + 12);
      drawEnemy(52, 54, false);
    } else {
      drawWeapon(w, 34, by + 12);
      drawEnemy(52, 54, true);
    }
  } else if (heroMood == H_HURT) {                 // hit by the "?" monster
    int shake = (t < 900) ? ((heroFrame % 2) ? 1 : -1) : 0;
    drawBot(21 + shake, by, 3);
    drawWeapon(w, 34 + shake, by + 12);
    drawEnemy(52, 54 + bob, false);
    if (t < 500) { oled.setCursor(44 - (int)(t * 12 / 500), by + 8); oled.print('!'); }
  } else if (heroMood == H_WIN) {                  // victory: jump and raise the weapon
    int jump = ((heroFrame / 2) % 2) * 3;
    drawBot(21, by - jump, 2);
    drawWeapon(w, 34, by - 4 - jump);
    if (heroFrame % 2) {
      oled.drawPixel(48, 34, SSD1306_WHITE); oled.drawPixel(56, 44, SSD1306_WHITE);
      oled.drawPixel(50, 56, SSD1306_WHITE); oled.drawPixel(60, 30, SSD1306_WHITE);
    }
  } else {                                         // idle: float and blink now and then
    drawBot(21, by + bob, (heroFrame % 20 == 0) ? 1 : 0);
    drawWeapon(w, 34, by + 12 + bob);
    drawEnemy(52, 54 + (1 - bob), false);
  }

  // 3. Pop-up text (only on correct / wrong / victory / knocked out)
  if (ko) {
    oledCenter("HELP!", 98, 2);
  } else if (heroCap1.length()) {
    oledCenter(heroCap1, 98, 1);
    oledCenter(heroCap2, 112, 1);
  }
  oled.display();
}

// Called by the game screens; decides which animation the character plays
void oledLines(const String& a, const String& b, const String& c, const String& d) {
  int m = H_IDLE;
  String c1 = "", c2 = "";
  if (b.startsWith("Correct"))                              { m = H_HIT;  c1 = "Correct!"; c2 = "+1 EXP"; }
  else if (b.startsWith("Wrong") || b.startsWith("Time"))   { m = H_HURT; c1 = b; c2 = String("Ans=") + correctAnswer; }
  else if (b.startsWith("CLEAR") || b.startsWith("YOU WIN")) { m = H_WIN; c1 = "VICTORY!"; }
  else if (b.startsWith("CHEAT"))                           { m = H_WIN;  c1 = "POWER UP!"; }
  else if (b.startsWith("PAUSED"))                          { m = H_SLEEP; }
  heroMood = m; heroCap1 = c1; heroCap2 = c2;
  heroSince = millis(); heroFrame = 0;
  oledDraw();
}

void oledTick() {                                  // animates the character (called from loop)
  if (!oledOK || millis() - lastOledTick < 150) return;
  lastOledTick = millis();
  heroFrame++;
  oledDraw();
}

// =====================================================
//  LEDs and sound
// =====================================================
void ledsOff() { digitalWrite(LED_GREEN, LOW); digitalWrite(LED_RED, LOW); }

void beep(int f, int d) { tone(BUZZER, f, d); }

void playCorrect() {
  tone(BUZZER, 1047, 100); delay(110);
  tone(BUZZER, 1319, 100); delay(110);
  tone(BUZZER, 1568, 200);
}

void playFanfare() {
  int notes[] = {784, 988, 1175, 1568};
  for (int i = 0; i < 4; i++) { tone(BUZZER, notes[i], 140); delay(160); }
}

// =====================================================
//  Treasure chest (SG90 servo) - opens after the BOSS is defeated
// =====================================================
bool chestOpen = false;

// A servo reads one pulse every 20 ms: 500 us = 0 deg, 2500 us = 180 deg
void servoPulses(int angle, int count) {
  int pw = map(angle, 0, 180, 500, 2500);
  for (int i = 0; i < count; i++) {
    digitalWrite(SERVO_PIN, HIGH);
    delayMicroseconds(pw);
    digitalWrite(SERVO_PIN, LOW);
    delayMicroseconds(20000 - pw);
  }
}

void moveChest(int from, int to) {                // slow sweep so the lid opens smoothly
  int step = (to > from) ? 5 : -5;
  for (int a = from; (step > 0) ? (a < to) : (a > to); a += step) servoPulses(a, 2);
  servoPulses(to, 10);                            // hold the final angle for 0.2 s
}

void openChest() {
  if (chestOpen) return;
  moveChest(CHEST_CLOSED, CHEST_OPEN);
  chestOpen = true;
  Serial.println("Treasure chest opened!");
}

void closeChest() {
  if (!chestOpen) return;
  moveChest(CHEST_OPEN, CHEST_CLOSED);
  chestOpen = false;
}

// =====================================================
//  TFT screens
// =====================================================
void drawTitle() {
  gbScreen(GB3, GB0);
  tftCenter("MATH QUEST", 28, 4, GB0);
  tftCenter("+  -  x  /", 72, 3, GB1);
  tftCenter(menuIdx == 0 ? "> CONTINUE  " : "  CONTINUE  ", 118, 3, GB0);
  tftCenter(menuIdx == 1 ? "> NEW GAME  " : "  NEW GAME  ", 152, 3, GB0);
  tftCenter(String("Saved: ") + stageLabel(unlocked), 192, 2, GB1);
  tftCenter("UP/DOWN  A:OK", 214, 1, GB1);
  oledLines("MATH QUEST", menuIdx == 0 ? ">CONTINUE" : ">NEW GAME", String("Saved: ") + stageLabel(unlocked), "A: OK");
}

void drawSelect() {
  gbScreen(GB3, GB0);
  tftCenter("SELECT STAGE", 20, 3, GB0);
  String arrows = String(selStage > 0 ? "<  " : "   ") + stageLabel(selStage) + (selStage < unlocked ? "  >" : "   ");
  tftCenter(arrows, 68, 3, GB0);
  tftCenter(STAGES[selStage].name, 108, 3, GB1);
  tftCenter(DIFF_NAME[diffOf(selStage)], 146, 2, GB1);
  tftCenter(String("WEAPONS: ") + weapons(), 178, 2, GB0);
  tftCenter("A:START   B:BACK", 210, 2, GB1);
  oledLines("SELECT STAGE", stageLabel(selStage), STAGES[selStage].name, "A:START B:BACK");
}

void drawOptions() {
  for (int i = 0; i < 4; i++) {
    int x = (i % 2) * 156 + 12;
    int y = 106 + (i / 2) * 60;
    bool sel = (i == cursor);
    tft.fillRoundRect(x, y, 140, 52, 8, sel ? GB0 : GB2);
    tft.drawRoundRect(x, y, 140, 52, 8, GB0);
    tft.setTextSize(1);
    tft.setTextColor(sel ? GB2 : GB1);
    tft.setCursor(x + 7, y + 6);
    tft.print((char)('A' + i));
    String s = String(options[i]);
    int16_t x1, y1; uint16_t w, h;
    tft.setTextSize(3);
    tft.setTextColor(sel ? GB3 : GB0);
    tft.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor(x + (140 - w) / 2, y + (52 - h) / 2);
    tft.print(s);
  }
}

void drawQuestionScreen() {
  gbScreen(GB3, GB0);
  tft.setTextSize(2);
  tft.setTextColor(GB0);
  tft.setCursor(14, 14);
  tft.print(curStage == BOSS ? String("BOSS") : String("ST") + (curStage + 1));
  tft.setCursor(110, 14);
  tft.print(String(stageCorrect) + "/" + goal());
  drawHearts(238, 12, GB0, GB2);
  tftCenter(questionText, 52, 4, GB0);
  drawOptions();
  if (timeLimit > 0) tft.fillRect(8, 224, 304, 8, GB1);
  oledLines(stageLabel(curStage) + "  " + stageCorrect + "/" + goal(), questionText,
            String("A") + options[0] + "  B" + options[1], String("C") + options[2] + "  D" + options[3]);
}

void drawFeedback(bool ok, int idx) {
  if (ok) {
    gbScreen(GB3, GB0);
    tftCenter("CORRECT!", 36, 5, GB0);
    tftCenter("+1", 98, 4, GB1);
    tftCenter(fullEquation, 150, 3, GB0);
    tftCenter(String("PROGRESS ") + stageCorrect + "/" + goal(), 196, 2, GB1);
    oledLines("", "Correct!", "+1", fullEquation);
  } else {
    gbScreen(GB0, GB2);
    tftCenter(idx < 0 ? "TIME UP!" : "WRONG!", 36, 5, GB3);
    tftCenter(String("Answer = ") + correctAnswer, 98, 3, GB2);
    tftCenter(fullEquation, 145, 3, GB3);
    drawHearts(124, 190, GB3, GB1);
    oledLines("", idx < 0 ? "Time up!" : "Wrong!", String("Answer = ") + correctAnswer, fullEquation);
  }
}

void drawClear() {
  gbScreen(GB3, GB0);
  tftCenter(curStage == BOSS ? "YOU WIN!" : "STAGE CLEAR!", 30, 4, GB0);
  tftCenter(clearMsg1, 100, 2, GB1);
  tftCenter(clearMsg2, 130, 2, GB1);
  tftCenter(String("WEAPONS: ") + weapons(), 170, 2, GB0);
  tftCenter("A: CONTINUE", 208, 2, GB1);
  oledLines("", curStage == BOSS ? "YOU WIN!" : "CLEAR!", clearMsg1, clearMsg2);
}

void drawPaused() {
  gbScreen(GB0, GB2);
  tftCenter("PAUSED", 80, 5, GB3);
  tftCenter("Waiting for dashboard...", 160, 2, GB2);
  oledLines("", "PAUSED", "", "");
}

// =====================================================
//  Question generator
// =====================================================
void makeQuestion(int st) {
  char op = STAGES[st].op;
  int n = STAGES[st].maxN;
  bool missing = false;
  if (op == 'M') {                         // BOSS: random operator, half are "missing number" questions
    const char ops[4] = {'+', '-', 'x', '/'};
    op = ops[random(4)];
    missing = random(2);
  }

  int a = random(1, n + 1), b = random(1, n + 1), r;
  if (op == '+')      r = a + b;
  else if (op == '-') { if (b > a) { int t = a; a = b; b = t; } r = a - b; }
  else if (op == 'x') r = a * b;
  else                { r = a; a = r * b; }   // division: a / b = r, always a whole number

  String sop = String(" ") + op + " ";
  fullEquation = String(a) + sop + b + " = " + r;

  if (!missing) {
    questionText = String(a) + sop + b + " = ?";
    correctAnswer = r;
  } else if (random(2)) {
    questionText = String(a) + sop + "? = " + r;
    correctAnswer = b;
  } else {
    questionText = String("?") + sop + b + " = " + r;
    correctAnswer = a;
  }

  // Put the correct answer in a random slot, fill the others with close wrong answers
  int minVal = missing ? 1 : 0;
  int spread = correctAnswer < 10 ? 4 : 8;
  correctIndex = random(4);
  options[correctIndex] = correctAnswer;
  for (int i = 0; i < 4; i++) {
    if (i == correctIndex) continue;
    int cand; bool ok;
    do {
      cand = correctAnswer + random(1, spread + 1) * (random(2) ? 1 : -1);
      ok = cand >= minVal;
      for (int j = 0; j < 4; j++)
        if (j != i && (j == correctIndex || j < i) && options[j] == cand) ok = false;
    } while (!ok);
    options[i] = cand;
  }
}

// =====================================================
//  ThingsBoard telemetry
// =====================================================
void publishJson(JsonDocument& doc) {
  if (!mqtt.connected()) return;
  char buf[900];
  serializeJson(doc, buf);
  mqtt.publish("v1/devices/me/telemetry", buf);
  Serial.print("[TX] "); Serial.println(buf);
}

float accuracyNow() {
  int n = correctCount + wrongCount;
  return n ? round(correctCount * 1000.0 / n) / 10.0 : 0;
}

void addCommon(JsonDocument& doc) {
  int st = (state == ASKING || state == FEEDBACK || state == PAUSED || state == CLEAR) ? curStage : selStage;
  doc["state"] = STATE_NAME[state];
  doc["stage"] = st + 1;
  doc["stageName"] = stageLabel(st) + " " + STAGES[st].name;
  doc["difficulty"] = DIFF_NAME[diffOf(st)];
  doc["unlockedStage"] = unlocked + 1;
  doc["weapons"] = weapons();
  doc["stageProgress"] = stageCorrect;
  doc["stageGoal"] = (st == BOSS) ? needCorrect + 2 : needCorrect;
  doc["timeLimit"] = timeLimit;
  doc["score"] = score;
  doc["correctCount"] = correctCount;
  doc["wrongCount"] = wrongCount;
  doc["accuracy"] = accuracyNow();
  doc["wrongStreak"] = wrongStreak;
  doc["needHelp"] = (wrongStreak >= 3);
  doc["hp"] = max(0, 3 - wrongStreak);
  doc["chestOpen"] = chestOpen;
}

void sendStatus() {
  JsonDocument doc;
  addCommon(doc);
  doc["currentQuestion"] = (state == ASKING) ? questionText : String("-");
  publishJson(doc);
}

void sendResult(bool ok, int idx, float rt, const char* source) {
  JsonDocument doc;
  addCommon(doc);
  doc["questionNo"] = questionNo;
  doc["question"] = questionText;
  doc["correctAnswer"] = correctAnswer;
  doc["playerAnswer"] = idx >= 0 ? options[idx] : -1;
  doc["result"] = ok ? "correct" : (idx < 0 ? "timeout" : "wrong");
  doc["correct"] = ok;
  doc["responseTime"] = round(rt * 10) / 10.0;
  doc["streak"] = streak;
  doc["source"] = source;
  publishJson(doc);
}

// =====================================================
//  Game flow
// =====================================================
void goSelect() { state = SELECT; drawSelect(); sendStatus(); }

void nextQuestion() {
  questionNo++;
  makeQuestion(curStage);
  cursor = 0;
  drawQuestionScreen();
  questionStart = millis();
  state = ASKING;
  sendStatus();
}

void startStage(int st) {
  curStage = selStage = st;
  stageCorrect = 0;
  wrongStreak = 0;
  ledsOff();
  nextQuestion();
}

void stageCleared() {
  clearMsg1 = ""; clearMsg2 = "";
  if (curStage == BOSS) {
    clearMsg1 = "YOU BEAT THE BOSS!";
    clearMsg2 = "ALL STAGES CLEAR";
    if (USE_RELAY) { digitalWrite(RELAY, HIGH); relayOffAt = millis() + 3000; }
  } else if (curStage >= unlocked) {        // first clear of this stage -> unlock the next one
    unlocked = curStage + 1;
    saveProgress();
    if (unlocked == 3)      { clearMsg1 = "NEW WEAPON:  -"; }
    else if (unlocked == 6) { clearMsg1 = "NEW WEAPON:  x"; clearMsg2 = "MEDIUM UNLOCKED!"; }
    else if (unlocked == 9) { clearMsg1 = "NEW WEAPON:  /"; clearMsg2 = "HARD (BOSS) UNLOCKED!"; }
    else                    { clearMsg1 = String("NEW NUMBERS: 1-") + STAGES[unlocked].maxN; }
  } else {
    clearMsg1 = "Stage replayed";
  }
  if (curStage < BOSS && curStage + 1 <= unlocked) selStage = curStage + 1;

  state = CLEAR;
  drawClear();
  playFanfare();
  if (curStage == BOSS) openChest();      // boss defeated -> open the treasure chest

  JsonDocument doc;
  addCommon(doc);
  doc["stageCleared"] = curStage + 1;
  publishJson(doc);
}

void afterFeedback() {
  ledsOff();
  if (stageCorrect >= goal()) stageCleared();
  else nextQuestion();
}

void submitAnswer(int idx, const char* source) {
  if (state != ASKING) return;
  float rt = (millis() - questionStart) / 1000.0;
  lastCorrect = (idx == correctIndex);

  if (lastCorrect) { score++; correctCount++; streak++; stageCorrect++; wrongStreak = 0; }
  else             { wrongCount++; wrongStreak++; streak = 0; }

  Serial.printf("[%s] %s -> %s (%.1f s)\n", stageLabel(curStage).c_str(),
                questionText.c_str(), lastCorrect ? "correct" : "wrong", rt);

  drawFeedback(lastCorrect, idx);
  if (lastCorrect) { digitalWrite(LED_GREEN, HIGH); playCorrect(); }
  else             { beep(200, 500); }

  sendResult(lastCorrect, idx, rt, source);

  if (USE_RELAY && lastCorrect && streak % 5 == 0) {   // reward for 5 correct in a row
    digitalWrite(RELAY, HIGH);
    relayOffAt = millis() + 3000;
  }
  state = FEEDBACK;
  feedbackUntil = millis() + 2200;
}

void pauseGame() {
  if (state != ASKING && state != FEEDBACK) return;
  stateBeforePause = state;
  pausedAt = millis();
  state = PAUSED;
  ledsOff();
  drawPaused();
  sendStatus();
}

void resumeGame() {
  if (stateBeforePause == ASKING) {
    questionStart += millis() - pausedAt;
    state = ASKING;
    drawQuestionScreen();
    sendStatus();
  } else {
    afterFeedback();
  }
}

void goTitle() {
  state = TITLE;
  menuIdx = 0;
  konamiPos = 0;
  closeChest();
  ledsOff();
  drawTitle();
  sendStatus();
}

void unlockAllStages() {
  unlocked = BOSS;
  saveProgress();
}

// Cheat code: UP UP DOWN DOWN LEFT RIGHT LEFT RIGHT B A
bool checkKonami(int b) {
  if (b == KONAMI[konamiPos]) konamiPos++;
  else konamiPos = (b == B_UP) ? (konamiPos == 2 ? 2 : 1) : 0;
  if (konamiPos < 10) return false;

  konamiPos = 0;
  unlockAllStages();
  gbScreen(GB0, GB2);
  tftCenter("CHEAT CODE!", 80, 4, GB3);
  tftCenter("ALL STAGES UNLOCKED", 140, 2, GB2);
  oledLines("", "CHEAT!", "ALL UNLOCKED", "");
  playFanfare();
  delay(1200);
  selStage = BOSS;
  goSelect();
  return true;
}

// =====================================================
//  Buttons
// =====================================================
void onPress(int b) {
  switch (state) {
    case TITLE:
      if (checkKonami(b)) return;
      if (b == B_UP || b == B_DOWN) { menuIdx = 1 - menuIdx; drawTitle(); beep(900, 30); }
      else if (b == BTN_A) {
        if (menuIdx == 1) { unlocked = 0; saveProgress(); }
        selStage = unlocked;
        beep(1200, 60);
        goSelect();
      }
      break;

    case SELECT:
      if (b == B_LEFT && selStage > 0)              { selStage--; drawSelect(); beep(900, 30); }
      else if (b == B_RIGHT && selStage < unlocked) { selStage++; drawSelect(); beep(900, 30); }
      else if (b == B_RIGHT)                        { beep(150, 120); }   // stage still locked
      else if (b == BTN_A)                          { beep(1200, 60); startStage(selStage); }
      else if (b == BTN_B)                          { goTitle(); }
      break;

    case ASKING:
      if (b == B_UP || b == B_DOWN)         { cursor ^= 2; drawOptions(); beep(900, 20); }
      else if (b == B_LEFT || b == B_RIGHT) { cursor ^= 1; drawOptions(); beep(900, 20); }
      else if (b == BTN_A)                  { submitAnswer(cursor, "button"); }
      break;

    case CLEAR:
      if (b == BTN_A) { beep(1200, 60); closeChest(); goSelect(); }
      break;

    default:
      break;
  }
}

void readButtons() {
  for (int i = 0; i < 6; i++) {
    bool now = digitalRead(BTN_PIN[i]);
    if (now != btnLast[i] && millis() - btnChange[i] > 50) {   // 50 ms debounce
      btnChange[i] = millis();
      btnLast[i] = now;
      if (now == LOW) onPress(i);
    }
  }
}

// =====================================================
//  RPC: commands from ThingsBoard
// =====================================================
void onMqtt(char* topic, byte* payload, unsigned int len) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len)) return;
  String method = doc["method"] | "";
  JsonVariantConst p = doc["params"];
  if (p.is<JsonObjectConst>() && !p["value"].isNull()) p = p["value"];
  Serial.println("[RPC RX] " + method);

  bool ok = true;
  if (method == "start") {
    if (state == PAUSED) resumeGame();
    else if (state == TITLE || state == SELECT || state == CLEAR) startStage(selStage);
  }
  else if (method == "pause") pauseGame();
  else if (method == "reset") goTitle();
  else if (method == "setDifficulty") {
    String v = p.as<String>(); v.toLowerCase();
    int target = -1;
    if (v.indexOf("easy") >= 0 || v == "1")        target = 0;
    else if (v.indexOf("medium") >= 0 || v == "2") target = 6;
    else if (v.indexOf("hard") >= 0 || v == "3")   target = BOSS;
    if (target < 0) ok = false;
    else {
      if (unlocked < target) { unlocked = target; saveProgress(); }   // parent unlocks remotely
      selStage = target;
      if (state == ASKING || state == FEEDBACK || state == PAUSED) startStage(target);
      else goSelect();
    }
  }
  else if (method == "setQuestions") {
    needCorrect = constrain(p.as<int>(), 1, 20);
    if (state == SELECT) drawSelect();
  }
  else if (method == "setTimeLimit") {
    timeLimit = constrain(p.as<int>(), 0, 60);
  }
  else if (method == "answer") {
    String v = p.as<String>(); v.toUpperCase();
    int idx = v.length() ? v[0] - 'A' : -1;
    if (state == ASKING && idx >= 0 && idx < 4) { cursor = idx; submitAnswer(idx, "dashboard"); }
    else ok = false;
  }
  else if (method == "unlockAll")     { unlockAllStages(); if (state == TITLE || state == SELECT) goSelect(); }
  else if (method == "resetProgress") { unlocked = 0; selStage = 0; saveProgress(); goTitle(); }
  else if (method == "openChest")     { openChest(); }
  else if (method == "closeChest")    { closeChest(); }
  else ok = false;

  // Reply to ThingsBoard (the number at the end of the topic is the request id)
  String t = String(topic);
  String id = t.substring(t.lastIndexOf('/') + 1);
  String reply = String("{\"ok\":") + (ok ? "true" : "false") + ",\"state\":\"" + STATE_NAME[state] + "\"}";
  mqtt.publish(("v1/devices/me/rpc/response/" + id).c_str(), reply.c_str());
  sendStatus();
}

void keepConnected() {          // non-blocking reconnect, retries every 5 s
  if (millis() - lastConnTry < 5000) return;
  if (WiFi.status() != WL_CONNECTED) { lastConnTry = millis(); WiFi.reconnect(); return; }
  if (!mqtt.connected()) {
    lastConnTry = millis();
    if (mqtt.connect("S3-quest", TB_TOKEN, NULL)) {           // access token is the MQTT username
      mqtt.subscribe("v1/devices/me/rpc/request/+");          // listen for RPC commands
      Serial.println("ThingsBoard connected");
      sendStatus();
    }
  }
}

// =====================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  randomSeed(esp_random());

  for (int i = 0; i < 6; i++) pinMode(BTN_PIN[i], INPUT_PULLUP);
  pinMode(LED_GREEN, OUTPUT); pinMode(LED_RED, OUTPUT); pinMode(RELAY, OUTPUT);
  ledsOff(); digitalWrite(RELAY, LOW);
  pinMode(SERVO_PIN, OUTPUT);
  servoPulses(CHEST_CLOSED, 25);              // make sure the chest starts closed

  prefs.begin("quest", false);
  unlocked = constrain(prefs.getInt("unlocked", 0), 0, BOSS);
  selStage = unlocked;

  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
  tft.init(240, 320);
  tft.setRotation(1);

  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.beginTransmission(0x3C);                 // the OLED is checked once at boot
  oledOK = (Wire.endTransmission() == 0) && oled.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false);
  if (oledOK) oled.setRotation(OLED_ROTATION);

  gbScreen(GB3, GB0);
  tftCenter("MATH QUEST", 80, 4, GB0);
  tftCenter("Connecting Wi-Fi...", 150, 2, GB1);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(300);
  Serial.println(WiFi.status() == WL_CONNECTED ? "Wi-Fi OK" : "Wi-Fi not connected, playing offline");

  mqtt.setServer(TB_HOST, 1883);
  mqtt.setCallback(onMqtt);
  mqtt.setBufferSize(1024);
  lastConnTry = millis() - 5000;
  keepConnected();

  drawTitle();
}

void loop() {
  keepConnected();
  mqtt.loop();
  readButtons();
  oledTick();
  unsigned long now = millis();

  // Countdown bar
  if (state == ASKING && timeLimit > 0) {
    long remain = timeLimit * 1000L - (long)(now - questionStart);
    if (remain <= 0) submitAnswer(-1, "timeout");
    else if (now - lastBarDraw > 200) {
      lastBarDraw = now;
      int w = 304L * remain / (timeLimit * 1000L);
      tft.fillRect(8 + w, 224, 304 - w, 8, GB3);
    }
  }

  // Feedback time: red LED blinks after a wrong answer
  if (state == FEEDBACK) {
    if (!lastCorrect) digitalWrite(LED_RED, (now / 150) % 2);
    if (now >= feedbackUntil) afterFeedback();
  }

  if (relayOffAt && now >= relayOffAt) { digitalWrite(RELAY, LOW); relayOffAt = 0; }
}
