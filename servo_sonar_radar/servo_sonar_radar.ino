/*
  Servo Sonar Radar + WiFi + 2.0" TFT (ST7789)
  - Servo sweeps the sonar 0..180 deg, a map of distances is built up
  - TFT: semicircle radar with fading blips + status bar  (set USE_TFT 0 if no screen)
  - Web page: same radar, change alert distance, mute the buzzer
  - Parking-sensor style alert: closer = faster beeping + red LED blinking

  Wiring:
    Servo : GND, 5V, signal -> D18
    Sonar : VCC->5V, GND->GND, Trig->D13, Echo->D14
    LED   : green D2, red D5 (220 ohm to GND)     Buzzer: D4
    TFT   : VCC->3.3V, GND, SCL->D19, SDA->D23, CS->D26, DC->D27, RST->D22

  Libraries: ESP32Servo, Adafruit ST7735 and ST7789 Library, Adafruit GFX
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

#define USE_TFT 1   // set to 0 while the screen is not connected

#if USE_TFT
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#endif

// ===== WiFi =====
#include "secrets.h"   // Wi-Fi name/password live here (see secrets.example.h)
const char* ssid = SECRET_WIFI_SSID;
const char* password = SECRET_WIFI_PASS;

// ===== Pins =====
const int servoPin = 18;
const int trigPin = 13;
const int echoPin = 14;
const int ledGreenPin = 2;
const int ledRedPin = 5;
const int buzzerPin = 4;

#define TFT_SCK  19
#define TFT_MOSI 23
#define TFT_CS   26
#define TFT_DC   27
#define TFT_RST  22

// ===== Buzzer (own LEDC channel so it does not disturb the servo) =====
const int buzzerChannel = 14;
const int buzzerFreq = 2000;
const int buzzerVolume = 20;

// ===== Radar settings =====
const int STEP_DEG = 3;                 // degrees per step
const int N_BINS = 61;                  // 0..180 step 3
const int MAXD = 100;                   // radar range (cm)
const unsigned long TICK_MS = 60;       // one measurement every 60 ms
const unsigned long FRAME_MS = 50;      // screen refresh
const unsigned long AGE_MS = 6000;      // blips fade out over this time
const bool MIRROR = false;              // true = flip left/right if the map is reversed

Servo servo;
WebServer server(80);
bool serverRunning = false;

float binDist[N_BINS];
unsigned long binTime[N_BINS];
int curBin = 0;
int dir = 1;
float nearest = -1;
float safeDistance = 20;
bool tooClose = false;
bool alarmOn = true;
unsigned long lastTick = 0, lastFrame = 0;

// ---------- Sonar ----------
float readDistanceCm() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 7000);   // ~120 cm max, keeps scanning fast
  if (duration == 0) return -1;
  return duration * 0.0343 / 2.0;
}

// ---------- One scan step ----------
void scanTick() {
  unsigned long now = millis();
  float d = readDistanceCm();
  binDist[curBin] = (d > 0 && d <= MAXD) ? d : -1;
  binTime[curBin] = now;

  nearest = -1;
  for (int i = 0; i < N_BINS; i++) {
    if (binDist[i] > 0 && now - binTime[i] < AGE_MS) {
      if (nearest < 0 || binDist[i] < nearest) nearest = binDist[i];
    }
  }
  tooClose = (nearest > 0 && nearest < safeDistance);

  if (curBin % 10 == 0) {
    Serial.printf("angle %3d  dist %.1f  nearest %.1f  %s\n",
                  curBin * STEP_DEG, d, nearest, tooClose ? "TOO CLOSE" : "clear");
  }

  int next = curBin + dir;
  if (next < 0 || next >= N_BINS) {
    dir = -dir;
    next = curBin + dir;
  }
  curBin = next;
  servo.write(curBin * STEP_DEG);
}

// ---------- LEDs + buzzer (non-blocking) ----------
void updateAlerts() {
  unsigned long now = millis();
  if (tooClose) {
    digitalWrite(ledGreenPin, LOW);
    int period = map((int)nearest, 0, (int)safeDistance, 100, 600);
    period = constrain(period, 100, 600);
    bool on = (now % period) < 50;
    digitalWrite(ledRedPin, on ? HIGH : LOW);
    ledcWrite(buzzerPin, (on && alarmOn) ? buzzerVolume : 0);
  } else {
    digitalWrite(ledGreenPin, HIGH);
    digitalWrite(ledRedPin, LOW);
    ledcWrite(buzzerPin, 0);
  }
}

// =====================================================================
//                              TFT
// =====================================================================
#if USE_TFT
Adafruit_ST7789 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);

const int RW = 320, RH = 156;           // radar canvas
const int RCX = 160, RCY = 150, RR = 145;
GFXcanvas16* radar = nullptr;

const uint16_t C_BLACK = 0x0000;
const uint16_t C_GREEN = 0x07E0;
const uint16_t C_RED = 0xF800;
const uint16_t C_DIM = 0x0400;

String shownNear = "", shownStat = "", shownThr = "", shownAng = "";
String ipStr = "no WiFi";

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void scr(int deg, float r, int &x, int &y) {
  float a = (MIRROR ? 180 - deg : deg) * PI / 180.0;
  x = RCX + r * cos(a);
  y = RCY - r * sin(a);
}

String pad(String s, int n) {
  while ((int)s.length() < n) s += ' ';
  return s;
}

void drawRadar() {
  if (!radar) return;
  unsigned long now = millis();
  radar->fillScreen(0);

  uint16_t ringC = rgb(0, 70, 0);
  for (int i = 1; i <= 4; i++) radar->drawCircle(RCX, RCY, RR * i / 4, ringC);
  for (int g = 0; g <= 180; g += 30) {
    int x, y;
    scr(g, RR, x, y);
    radar->drawLine(RCX, RCY, x, y, ringC);
  }
  radar->drawFastHLine(0, RCY, RW, ringC);

  // alert ring
  radar->drawCircle(RCX, RCY, RR * min(safeDistance, (float)MAXD) / MAXD, C_RED);

  // sweep line with fading trail
  for (int k = 9; k >= 0; k--) {
    int deg = curBin * STEP_DEG - dir * k * STEP_DEG;
    int x, y;
    scr(deg, RR, x, y);
    radar->drawLine(RCX, RCY, x, y, rgb(0, 40 + (9 - k) * 20, 0));
  }

  // blips, fading with age
  for (int i = 0; i < N_BINS; i++) {
    unsigned long age = now - binTime[i];
    if (binDist[i] > 0 && age < AGE_MS) {
      float f = 1.0 - (float)age / AGE_MS;
      uint8_t lv = 40 + (uint8_t)(215 * f);
      uint16_t col = (binDist[i] < safeDistance) ? rgb(lv, 0, 0) : rgb(0, lv, 0);
      int x, y;
      scr(i * STEP_DEG, RR * binDist[i] / MAXD, x, y);
      radar->fillCircle(x, y, 3, col);
    }
  }

  radar->setTextSize(1);
  radar->setTextColor(rgb(0, 120, 0));
  radar->setCursor(2, 2);
  radar->print("SONAR RADAR  range 100cm");

  tft.drawRGBBitmap(0, 0, radar->getBuffer(), RW, RH);
}

void drawPanelStatic() {
  tft.fillRect(0, 156, 320, 84, C_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(C_DIM, C_BLACK);
  tft.setCursor(4, 164);   tft.print("NEAREST (cm)");
  tft.setCursor(100, 164); tft.print("STATUS");
  tft.setCursor(224, 164); tft.print("ALERT AT");
  tft.setCursor(4, 224);   tft.print("IP " + ipStr);
  shownNear = shownStat = shownThr = shownAng = "";
}

void drawPanel() {
  String ns = nearest > 0 ? String(nearest, 1) : String("---");
  String ss = tooClose ? "CLOSE!" : "CLEAR";
  String ts = String((int)safeDistance) + "cm";
  uint16_t col = tooClose ? C_RED : C_GREEN;

  if (ns != shownNear || ss != shownStat) {
    shownNear = ns;
    shownStat = ss;
    tft.setTextSize(3);
    tft.setTextColor(col, C_BLACK);
    tft.setCursor(4, 180);   tft.print(pad(ns, 5));
    tft.setCursor(100, 180); tft.print(pad(ss, 6));
  }
  if (ts != shownThr) {
    shownThr = ts;
    tft.setTextSize(3);
    tft.setTextColor(C_RED, C_BLACK);
    tft.setCursor(224, 180);
    tft.print(pad(ts, 5));
  }
  char buf[12];
  snprintf(buf, sizeof(buf), "ANG %3d", curBin * STEP_DEG);
  String as = String(buf);
  if (as != shownAng) {
    shownAng = as;
    tft.setTextSize(1);
    tft.setTextColor(C_DIM, C_BLACK);
    tft.setCursor(260, 224);
    tft.print(as);
  }
}
#endif

// =====================================================================
//                              Web
// =====================================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Sonar Radar</title>
<style>
  body{margin:0;background:#020a03;color:#39ff6a;font-family:"Courier New",monospace;text-align:center;padding:16px;}
  h1{font-size:20px;letter-spacing:4px;margin:6px 0 10px;text-shadow:0 0 8px #39ff6a;}
  canvas{background:#031208;border:2px solid #1c7a35;border-radius:12px;max-width:96vw;box-shadow:0 0 18px #0b4d1c;}
  #dist{font-size:40px;margin-top:10px;text-shadow:0 0 10px #39ff6a;}
  #status{font-size:20px;letter-spacing:3px;margin-top:4px;}
  .danger{color:#ff4d4d;text-shadow:0 0 10px #ff4d4d;}
  .panel{margin:14px auto 0;max-width:340px;border:1px solid #1c7a35;border-radius:10px;padding:12px;}
  input{width:80px;padding:6px;background:#031208;color:#39ff6a;border:1px solid #39ff6a;border-radius:6px;text-align:center;font-family:inherit;font-size:16px;}
  button{margin-left:8px;padding:7px 16px;background:#39ff6a;color:#031208;border:none;border-radius:6px;font-family:inherit;font-weight:bold;cursor:pointer;}
</style>
</head>
<body>
<h1>SONAR RADAR</h1>
<canvas id="radar" width="320" height="175"></canvas>
<div id="dist">-- cm</div>
<div id="status">SCANNING</div>
<div class="panel">
  ALERT DISTANCE (cm)<br><br>
  <input type="number" id="thr" value="20">
  <button onclick="setThr()">SET</button>
  <br><br>
  <button id="alarmBtn" onclick="toggleAlarm()" style="margin-left:0">BUZZER: ON</button>
</div>

<script>
var cv=document.getElementById('radar'), ctx=cv.getContext('2d');
var CX=160, CY=165, R=150, MAXD=100, N=61;
var st={a:90,dir:1,thr:20,near:-1,close:false,alarm:true,mir:false,d:[]};

function pt(deg,r){
  var a=(st.mir?180-deg:deg)*Math.PI/180;
  return [CX+r*Math.cos(a), CY-r*Math.sin(a)];
}
function draw(){
  ctx.clearRect(0,0,320,175);
  ctx.lineWidth=1; ctx.strokeStyle='#1c7a35';
  for(var i=1;i<=4;i++){ctx.beginPath();ctx.arc(CX,CY,R*i/4,Math.PI,2*Math.PI);ctx.stroke();}
  for(var g=0;g<=180;g+=30){var p=pt(g,R);ctx.beginPath();ctx.moveTo(CX,CY);ctx.lineTo(p[0],p[1]);ctx.stroke();}
  ctx.beginPath();ctx.moveTo(CX-R,CY);ctx.lineTo(CX+R,CY);ctx.stroke();

  ctx.strokeStyle='#ff4d4d'; ctx.setLineDash([5,5]);
  ctx.beginPath();ctx.arc(CX,CY,R*Math.min(st.thr,MAXD)/MAXD,Math.PI,2*Math.PI);ctx.stroke();
  ctx.setLineDash([]);

  for(var k=0;k<12;k++){
    var q=pt(st.a-st.dir*k*3,R);
    ctx.strokeStyle='rgba(57,255,106,'+(0.55-k*0.045)+')'; ctx.lineWidth=2;
    ctx.beginPath();ctx.moveTo(CX,CY);ctx.lineTo(q[0],q[1]);ctx.stroke();
  }
  for(var j=0;j<N;j++){
    var dd=st.d[j];
    if(dd>0){
      var deg=j*3, behind=(st.a-deg)*st.dir;
      if(behind<0) behind+=360;
      ctx.globalAlpha=Math.max(0.15,1-behind/360);
      var b=pt(deg,R*dd/MAXD);
      ctx.fillStyle=(dd<st.thr)?'#ff4d4d':'#39ff6a';
      ctx.beginPath();ctx.arc(b[0],b[1],4,0,2*Math.PI);ctx.fill();
      ctx.globalAlpha=1;
    }
  }
}
function poll(){
  fetch('/data').then(function(r){return r.json();}).then(function(d){
    st=d; draw();
    document.getElementById('dist').innerText = d.near>0 ? d.near.toFixed(1)+' cm' : 'NO TARGET';
    var s=document.getElementById('status');
    s.innerText = d.close ? '!! TARGET TOO CLOSE !!' : 'AREA CLEAR';
    s.className = d.close ? 'danger' : '';
    var inp=document.getElementById('thr');
    if(document.activeElement!==inp) inp.value=d.thr;
    document.getElementById('alarmBtn').innerText='BUZZER: '+(d.alarm?'ON':'OFF');
  }).catch(function(){});
}
function setThr(){ fetch('/setThreshold?value='+document.getElementById('thr').value); }
function toggleAlarm(){ fetch('/alarm?on='+(st.alarm?0:1)); }
draw();
setInterval(poll,150);
poll();
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleData() {
  unsigned long now = millis();
  String json;
  json.reserve(600);
  json += "{\"a\":";      json += String(curBin * STEP_DEG);
  json += ",\"dir\":";    json += String(dir);
  json += ",\"thr\":";    json += String((int)safeDistance);
  json += ",\"near\":";   json += String(nearest, 1);
  json += ",\"close\":";  json += (tooClose ? "true" : "false");
  json += ",\"alarm\":";  json += (alarmOn ? "true" : "false");
  json += ",\"mir\":";    json += (MIRROR ? "true" : "false");
  json += ",\"d\":[";
  for (int i = 0; i < N_BINS; i++) {
    if (i) json += ',';
    bool ok = binDist[i] > 0 && now - binTime[i] < AGE_MS;
    json += String(ok ? (int)(binDist[i] + 0.5) : -1);
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleSetThreshold() {
  if (server.hasArg("value")) {
    float v = server.arg("value").toFloat();
    if (v >= 5 && v <= MAXD) safeDistance = v;
  }
  server.send(200, "text/plain", "OK");
}

void handleAlarm() {
  if (server.hasArg("on")) alarmOn = (server.arg("on") == "1");
  server.send(200, "text/plain", "OK");
}

void startWeb() {
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/setThreshold", handleSetThreshold);
  server.on("/alarm", handleAlarm);
  server.begin();
  serverRunning = true;
  Serial.println("Web server: http://" + WiFi.localIP().toString());
#if USE_TFT
  ipStr = WiFi.localIP().toString();
  drawPanelStatic();
#endif
}

// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(ledGreenPin, OUTPUT);
  pinMode(ledRedPin, OUTPUT);
  digitalWrite(ledGreenPin, LOW);
  digitalWrite(ledRedPin, LOW);

  for (int i = 0; i < N_BINS; i++) { binDist[i] = -1; binTime[i] = 0; }

  // servo first, then the buzzer on a separate LEDC channel
  servo.setPeriodHertz(50);
  servo.attach(servoPin, 500, 2400);
  servo.write(0);
  ledcAttachChannel(buzzerPin, buzzerFreq, 8, buzzerChannel);
  ledcWrite(buzzerPin, 0);
  delay(700);   // let the servo reach 0 deg

#if USE_TFT
  SPI.begin(TFT_SCK, -1, TFT_MOSI, -1);
  tft.init(240, 320);
  tft.setSPISpeed(27000000);
  tft.setRotation(1);               // landscape 320x240
  tft.fillScreen(C_BLACK);
  tft.setTextColor(C_GREEN, C_BLACK);
  tft.setTextSize(2);
  tft.setCursor(40, 110);
  tft.print("Connecting WiFi...");

  radar = new GFXcanvas16(RW, RH);  // allocate before WiFi starts
  if (!radar || !radar->getBuffer()) {
    Serial.println("Radar canvas allocation failed");
    radar = nullptr;
  }
#endif

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);
  Serial.printf("Connecting to \"%s\"", ssid);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("IP address: " + WiFi.localIP().toString());
    startWeb();
  } else {
    Serial.printf("WiFi failed, status = %d (will keep retrying)\n", WiFi.status());
  }

#if USE_TFT
  tft.fillScreen(C_BLACK);
  drawPanelStatic();
#endif
}

void loop() {
  if (serverRunning) server.handleClient();

  unsigned long now = millis();

  // WiFi watchdog: reconnect if needed, print the IP every 5 s
  static unsigned long lastWifiCheck = 0;
  if (now - lastWifiCheck > 5000) {
    lastWifiCheck = now;
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("IP: " + WiFi.localIP().toString());
      if (!serverRunning) startWeb();
    } else {
      Serial.printf("WiFi not connected (status %d), retrying...\n", WiFi.status());
      WiFi.reconnect();
    }
  }

  if (now - lastTick >= TICK_MS) {
    lastTick = now;
    scanTick();
  }

  updateAlerts();

#if USE_TFT
  if (now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    drawRadar();
    drawPanel();
  }
#endif
}