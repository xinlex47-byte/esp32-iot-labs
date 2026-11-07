#include <Wire.h> 
#include <Adafruit_GFX.h> 
#include <Adafruit_SSD1306.h> 
#include "BluetoothSerial.h"

#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 64 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1); 

int ledPin = 2;
int buzzerPin = 4; 
int relayPin = 18; 

int buzzerFreq = 5000;
int buzzerResolution = 8;
int buzzerVolume = 20;   // Low volume

BluetoothSerial SerialBT;

unsigned long lastUpdate = 0;
float temperature = 25.0;
float humidity = 50.0;

void setup() { 
  Serial.begin(115200); 
  delay(1000);

  pinMode(ledPin, OUTPUT); 
  pinMode(relayPin, OUTPUT); 
  digitalWrite(relayPin, LOW);
  ledcAttach(buzzerPin, buzzerFreq, buzzerResolution);

  randomSeed(analogRead(0));

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed")); 
    for(;;); 
  } 
  display.clearDisplay();

  SerialBT.begin("ESP32test");   // Bluetooth device name
  Serial.println("Bluetooth started. Send:");
  Serial.println("a = LED ON, b = LED OFF");
  Serial.println("c = Buzzer ON, d = Buzzer OFF");
  Serial.println("e = Relay ON, f = Relay OFF");
} 

void updateDisplay() {
  display.clearDisplay(); 
  display.setTextSize(1);
  display.setTextColor(WHITE); 
  display.setCursor(0,0); 
  display.println("Temp: " + String(temperature) + " C"); 
  display.println("Hum: " + String(humidity) + " %"); 
  display.display(); 
}

void loop() { 
  // Update simulated temperature/humidity every 2 seconds
  if (millis() - lastUpdate > 2000) {
    temperature = random(200, 350) / 10.0;
    humidity = random(400, 700) / 10.0;
    updateDisplay();
    lastUpdate = millis();
  }

  // Handle Bluetooth commands
  if (SerialBT.available()) {
    char message = SerialBT.read();
    Serial.write(message);

    switch (message) {
      case 'a':
        digitalWrite(ledPin, HIGH);
        SerialBT.println("LED Turned ON");
        break;
      case 'b':
        digitalWrite(ledPin, LOW);
        SerialBT.println("LED Turned OFF");
        break;
      case 'c':
        ledcWriteTone(buzzerPin, 1000);
        ledcWrite(buzzerPin, buzzerVolume);
        SerialBT.println("Buzzer Turned ON");
        break;
      case 'd':
        ledcWrite(buzzerPin, 0);
        SerialBT.println("Buzzer Turned OFF");
        break;
      case 'e':
        digitalWrite(relayPin, HIGH);
        SerialBT.println("Relay Turned ON");
        break;
      case 'f':
        digitalWrite(relayPin, LOW);
        SerialBT.println("Relay Turned OFF");
        break;
      default:
        SerialBT.println("Invalid Input");
        break;
    }
  }

  delay(20);
}