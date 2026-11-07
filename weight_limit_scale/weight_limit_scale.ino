#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "HX711.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define LOADCELL_DOUT_PIN 23
#define LOADCELL_SCK_PIN 19
HX711 scale;

float calibrationFactor = 501.7;

int ledGreenPin = 2;
int ledRedPin = 5;
int buzzerPin = 4;
int relayPin = 18;

int buzzerFreq = 5000;
int buzzerResolution = 8;
int buzzerVolume = 20;   // Low volume (0-255)

float targetWeight = 200.0;   // Changed to 200g

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(ledGreenPin, OUTPUT);
  pinMode(ledRedPin, OUTPUT);
  pinMode(relayPin, OUTPUT);
  ledcAttach(buzzerPin, buzzerFreq, buzzerResolution);

  digitalWrite(ledGreenPin, LOW);
  digitalWrite(ledRedPin, LOW);
  digitalWrite(relayPin, LOW);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearDisplay();

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibrationFactor);
  scale.tare();

  Serial.println("Scale ready.");
}

void loop() {
  float weight = scale.get_units(5);
  if (weight < 0) weight = 0;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Weight: " + String(weight, 1) + " g");
  display.println("Target: " + String(targetWeight, 1) + " g");

  if (weight > targetWeight) {
    display.println("Status: OVER LIMIT!");
    digitalWrite(ledRedPin, HIGH);
    digitalWrite(ledGreenPin, LOW);
    digitalWrite(relayPin, LOW);
    ledcWriteTone(buzzerPin, 1000);
    ledcWrite(buzzerPin, buzzerVolume);
  } else {
    display.println("Status: OK");
    digitalWrite(ledRedPin, LOW);
    digitalWrite(ledGreenPin, HIGH);
    digitalWrite(relayPin, HIGH);
    ledcWrite(buzzerPin, 0);
  }

  display.display();

  Serial.print("Weight: ");
  Serial.print(weight);
  Serial.println(" g");

  delay(300);
}