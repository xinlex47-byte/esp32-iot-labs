// serial_test.ino —— 只测网页 → ESP32 通讯
const int LED = 2;  // DevKit V1 板载蓝灯

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  Serial.println("READY");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') return;   // 忽略换行
    Serial.print("GOT: ");
    Serial.println(c);
    if (c == 'O') digitalWrite(LED, HIGH);
    if (c == 'S' || c == 'N') digitalWrite(LED, LOW);
    if (c == 'F') {                        // F 让灯快闪 3 下
      for (int i = 0; i < 3; i++) {
        digitalWrite(LED, HIGH); delay(100);
        digitalWrite(LED, LOW);  delay(100);
      }
    }
  }
}