 /*
  Sonar Distance Monitor + WiFi Web Server
  ------------------------------------------
  - HC-SR04 measures distance continuously
  - OLED shows distance + a bar graph
  - LCD shows SAFE / TOO CLOSE status text
  - Buzzer (low volume) + Red LED alert when too close
  - Green LED when distance is safe
  - Relay toggles together with the alert (e.g. could trigger a fan/light)
  - WiFi web page (radar style) shows live distance and lets you change
    the alert distance remotely

  Wiring:
    Sonar : VCC->5V, GND->GND, Trig->D13, Echo->D14
    Green LED : D2 (through 220ohm resistor to GND)
    Red LED   : D5 (through 220ohm resistor to GND)
    Buzzer    : D4 (positive), GND (negative)
    Relay IN  : D18
    OLED (I2C): VCC->3.3V, GND->GND, SDA->D21, SCL->D22  (addr 0x3C)
    LCD (I2C) : VCC->5V,   GND->GND, SDA->D21, SCL->D22  (addr 0x27)
      (OLED and LCD share the same SDA/SCL lines - different I2C address)

  Required Libraries:
    - Adafruit SSD1306, Adafruit GFX
    - LiquidCrystal I2C (by Frank de Brabander)
    - WiFi, WebServer (built into ESP32 core)
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LiquidCrystal_I2C.h>

// ===== WiFi credentials =====
#include "secrets.h"   // Wi-Fi name/password live here (see secrets.example.h)
const char* ssid = SECRET_WIFI_SSID;
const char* password = SECRET_WIFI_PASS;

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
const int buzzerVolume = 20;  // low volume

// ===== Displays =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
LiquidCrystal_I2C lcd(0x27, 16, 2);

WebServer server(80);

// ===== State =====
float distanceCm = 0;
float safeDistance = 20.0;   // cm, adjustable from web page
bool tooClose = false;

unsigned long lastRead = 0;
const unsigned long readInterval = 200;  // read sonar every 200ms

// ---------- Read distance from HC-SR04 ----------
float readDistanceCm() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000); // 30ms timeout (~5m range)
  if (duration == 0) return -1; // no echo received

  float distance = duration * 0.0343 / 2.0; // speed of sound = 343 m/s
  return distance;
}

// ---------- OLED: distance number + bar graph ----------
void updateOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);

  if (distanceCm < 0) {
    display.println("Distance: ---");
  } else {
    display.println("Distance: " + String(distanceCm, 1) + " cm");
  }
  display.println(tooClose ? "Status: TOO CLOSE!" : "Status: SAFE");

  // Bar graph: closer = longer bar, cap at 100cm for scale
  int barMax = 100;
  float d = (distanceCm < 0) ? barMax : distanceCm;
  if (d > barMax) d = barMax;
  int filled = map((int)(barMax - d), 0, barMax, 0, SCREEN_WIDTH - 4);

  display.drawRect(0, 40, SCREEN_WIDTH, 16, WHITE);
  display.fillRect(2, 42, filled, 12, WHITE);

  display.display();
}

// ---------- LCD: simple status text ----------
void updateLCD() {
  lcd.setCursor(0, 0);
  lcd.print("Dist: ");
  if (distanceCm < 0) {
    lcd.print("---   ");
  } else {
    lcd.print(String(distanceCm, 1) + "cm  ");
  }

  lcd.setCursor(0, 1);
  lcd.print(tooClose ? "!! TOO CLOSE !! " : "Status: SAFE    ");
}

// ---------- Alerts ----------
void updateAlerts() {
  if (tooClose) {
    digitalWrite(ledRedPin, HIGH);
    digitalWrite(ledGreenPin, LOW);
    digitalWrite(relayPin, HIGH);
    ledcWriteTone(buzzerPin, 1000);
    ledcWrite(buzzerPin, buzzerVolume);
  } else {
    digitalWrite(ledRedPin, LOW);
    digitalWrite(ledGreenPin, HIGH);
    digitalWrite(relayPin, LOW);
    ledcWrite(buzzerPin, 0);
  }
}

// ---------- Web page (radar style) ----------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Sonar Radar</title>
<style>
  body{margin:0;background:#020a03;color:#39ff6a;font-family:"Courier New",monospace;text-align:center;padding:16px;}
  h1{font-size:20px;letter-spacing:4px;margin:6px 0 10px;text-shadow:0 0 8px #39ff6a;}
  canvas{background:#031208;border:2px solid #1c7a35;border-radius:50%;max-width:92vw;box-shadow:0 0 25px #0b4d1c inset,0 0 18px #0b4d1c;}
  #dist{font-size:44px;margin-top:10px;text-shadow:0 0 10px #39ff6a;}
  #status{font-size:20px;letter-spacing:3px;margin-top:4px;}
  .danger{color:#ff4d4d;text-shadow:0 0 10px #ff4d4d;}
  .panel{margin:16px auto 0;max-width:320px;border:1px solid #1c7a35;border-radius:10px;padding:12px;}
  input{width:80px;padding:6px;background:#031208;color:#39ff6a;border:1px solid #39ff6a;border-radius:6px;text-align:center;font-family:inherit;font-size:16px;}
  button{margin-left:8px;padding:7px 16px;background:#39ff6a;color:#031208;border:none;border-radius:6px;font-family:inherit;font-weight:bold;cursor:pointer;}
</style>
</head>
<body>
<h1>SONAR RADAR</h1>
<canvas id="radar" width="320" height="320"></canvas>
<div id="dist">-- cm</div>
<div id="status">SCANNING</div>
<div class="panel">
  ALERT DISTANCE (cm)<br><br>
  <input type="number" id="thresholdInput" value="20">
  <button onclick="updateThreshold()">SET</button>
</div>

<script>
var cv = document.getElementById('radar'), ctx = cv.getContext('2d');
var cx = 160, cy = 160, R = 150, MAXD = 100;
var angle = 0, dist = -1, tooClose = false, threshold = 20;

function draw(){
  ctx.clearRect(0,0,320,320);

  // rings
  ctx.strokeStyle = '#1c7a35'; ctx.lineWidth = 1;
  for (var i=1;i<=4;i++){ ctx.beginPath(); ctx.arc(cx,cy,R*i/4,0,Math.PI*2); ctx.stroke(); }
  ctx.beginPath(); ctx.moveTo(cx-R,cy); ctx.lineTo(cx+R,cy); ctx.moveTo(cx,cy-R); ctx.lineTo(cx,cy+R); ctx.stroke();

  // alert-distance ring
  var tr = R * Math.min(threshold, MAXD) / MAXD;
  ctx.strokeStyle = '#ff4d4d'; ctx.setLineDash([5,5]);
  ctx.beginPath(); ctx.arc(cx,cy,tr,0,Math.PI*2); ctx.stroke(); ctx.setLineDash([]);

  // sweep (fading trail)
  for (var k=0;k<40;k++){
    var a = angle - k*0.03;
    ctx.strokeStyle = 'rgba(57,255,106,' + (0.5 - k*0.0125) + ')';
    ctx.lineWidth = 2;
    ctx.beginPath(); ctx.moveTo(cx,cy);
    ctx.lineTo(cx + R*Math.cos(a), cy + R*Math.sin(a)); ctx.stroke();
  }

  // blip (fixed direction: straight up, the sensor only measures distance)
  if (dist > 0){
    var r = R * Math.min(dist, MAXD) / MAXD;
    var bx = cx, by = cy - r;
    ctx.fillStyle = tooClose ? '#ff4d4d' : '#39ff6a';
    ctx.shadowColor = ctx.fillStyle; ctx.shadowBlur = 14;
    ctx.beginPath(); ctx.arc(bx,by,7,0,Math.PI*2); ctx.fill();
    ctx.shadowBlur = 0;
  }

  angle += 0.04;
  requestAnimationFrame(draw);
}
draw();

function refreshData(){
  fetch('/data').then(function(r){return r.json();}).then(function(d){
    dist = d.distance; tooClose = d.tooClose; threshold = d.threshold;
    document.getElementById('dist').innerText = d.distance > 0 ? d.distance.toFixed(1) + ' cm' : '-- cm';
    var st = document.getElementById('status');
    st.innerText = d.tooClose ? '!! TARGET TOO CLOSE !!' : 'AREA CLEAR';
    st.className = d.tooClose ? 'danger' : '';
    var inp = document.getElementById('thresholdInput');
    if (document.activeElement !== inp) inp.value = d.threshold;
  }).catch(function(){});
}
function updateThreshold(){
  var v = document.getElementById('thresholdInput').value;
  fetch('/setThreshold?value=' + v);
}
setInterval(refreshData, 500);
refreshData();
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

// Returns live readings as JSON, used by the page's JS polling - does not reload the page
void handleData() {
  String json = "{";
  json += "\"distance\":" + String(distanceCm, 1) + ",";
  json += "\"tooClose\":" + String(tooClose ? "true" : "false") + ",";
  json += "\"threshold\":" + String((int)safeDistance);
  json += "}";
  server.send(200, "application/json", json);
}

void handleSetThreshold() {
  if (server.hasArg("value")) {
    safeDistance = server.arg("value").toFloat();
  }
  server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(ledGreenPin, OUTPUT);
  pinMode(ledRedPin, OUTPUT);
  pinMode(relayPin, OUTPUT);
  ledcAttach(buzzerPin, buzzerFreq, buzzerResolution);

  digitalWrite(ledGreenPin, LOW);
  digitalWrite(ledRedPin, LOW);
  digitalWrite(relayPin, LOW);

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearDisplay();
  display.display();

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");

  // WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("IP:");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP().toString());
  delay(2000);

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/setThreshold", handleSetThreshold);
  server.begin();
}

void loop() {
  server.handleClient();

  unsigned long now = millis();
  if (now - lastRead >= readInterval) {
    lastRead = now;

    float d = readDistanceCm();
    if (d > 0) distanceCm = d;

    tooClose = (distanceCm > 0 && distanceCm < safeDistance);

    updateOLED();
    updateLCD();
    updateAlerts();

    Serial.print("Distance: ");
    Serial.print(distanceCm);
    Serial.print(" cm | Threshold: ");
    Serial.print(safeDistance);
    Serial.println(tooClose ? " | TOO CLOSE" : " | SAFE");
  }
}