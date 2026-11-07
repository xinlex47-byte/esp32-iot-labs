#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

int buzzerPin = 4;
int ledPin = 2;

int freq = 5000;
int resolution = 8;
int volume = 40;

// Note frequencies
#define NOTE_G4  392
#define NOTE_F4  349
#define NOTE_EB4 311
#define NOTE_D4  294
#define NOTE_C4  262
#define NOTE_BB4 466

int melody[] = {
  NOTE_G4, NOTE_G4, NOTE_G4, NOTE_EB4,
  NOTE_F4, NOTE_F4, NOTE_F4, NOTE_D4,

  NOTE_G4, NOTE_G4, NOTE_G4, NOTE_EB4,
  NOTE_BB4, NOTE_BB4, NOTE_BB4, NOTE_BB4,
  NOTE_G4, NOTE_G4, NOTE_G4, NOTE_G4,

  NOTE_D4
};

int noteDurations[] = {
  300, 300, 300, 900,
  300, 300, 300, 900,

  300, 300, 300, 900,
  200, 200, 200, 200,
  200, 200, 200, 200,

  1200
};

void setup() {
  ledcAttach(buzzerPin, freq, resolution);
  pinMode(ledPin, OUTPUT);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Symphony No.5");
  lcd.setCursor(0, 1);
  lcd.print("by Beethoven");
  delay(2000);
}

void loop() {
  int songLength = sizeof(melody) / sizeof(melody[0]);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Now Playing...");

  for (int i = 0; i < songLength; i++) {
    // Show progress bar on second line
    lcd.setCursor(0, 1);
    int progress = map(i, 0, songLength - 1, 0, 16);
    for (int j = 0; j < 16; j++) {
      lcd.print(j < progress ? (char)255 : ' ');  // 255 = filled block char
    }

    digitalWrite(ledPin, HIGH);
    playTone(melody[i], noteDurations[i]);
    digitalWrite(ledPin, LOW);
    delay(50);
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Finished!");
  lcd.setCursor(0, 1);
  lcd.print("Replaying in 2s");

  delay(2000);
}

void playTone(int frequency, int duration) {
  ledcWriteTone(buzzerPin, frequency);
  ledcWrite(buzzerPin, volume);
  delay(duration);
  ledcWrite(buzzerPin, 0);
}