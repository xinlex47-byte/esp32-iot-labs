int ledPin = 2;
int ledPin2 = 4;
int ledPin3 = 5;

void setup() {
  pinMode(ledPin, OUTPUT);   // Set pin as output
  pinMode(ledPin2, OUTPUT);  // Set pin as output
  pinMode(ledPin3, OUTPUT);  // Set pin as output
}

void loop() {
  digitalWrite(ledPin, HIGH);   // Turn on LED 1
  delay(150);                   // Wait
  digitalWrite(ledPin, LOW);    // Turn off LED 1

  digitalWrite(ledPin2, HIGH);  // Turn on LED 2
  delay(150);                   // Wait
  digitalWrite(ledPin2, LOW);   // Turn off LED 2

  digitalWrite(ledPin3, HIGH);  // Turn on LED 3
  delay(150);                   // Wait
  digitalWrite(ledPin3, LOW);   // Turn off LED 3
}