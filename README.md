# ESP32 IoT Labs

A collection of ESP32 projects I built while learning embedded systems and IoT: sensors, displays, wireless control, and cloud dashboards (Blynk, ThingsBoard). Most of them started from a course lab, and I extended each one with my own features.

For my main project, see **[hamster-pet](https://github.com/xinlex47-byte/hamster-pet)**, an AI virtual pet with on-device image recognition.

## Projects

| Project | What it does | Key parts / tech |
|---|---|---|
| [`math_quest`](math_quest) | **Game Boy-style math adventure game.** 10 stages from addition to a mixed-operator BOSS, a hero with HP and "operator weapons", and a servo-driven treasure chest that opens when you win. Progress is saved to flash, and there's a Konami-code cheat. Can be controlled remotely from a ThingsBoard dashboard over MQTT RPC. | ESP32-S3, ST7789 TFT + SSD1306 OLED, 6 buttons, SG90 servo, MQTT, ThingsBoard |
| [`dual_sense_bin`](dual_sense_bin) | **Smart rubbish bin with two sensors.** A load cell measures weight and an ultrasonic sensor measures fill volume. Combining them gives four states: `OK`, `HEAVY`, `BULKY` (full but light, so flatten it) and `FULL`. Sends phone notifications through Blynk. | HX711, HC-SR04 (median filter), ST7789 TFT, Blynk |
| [`servo_sonar_radar`](servo_sonar_radar) | **Sweeping sonar radar.** A servo sweeps an ultrasonic sensor 0–180° and draws a radar with fading blips on a TFT and on a web page. Parking-sensor style alerts: the closer the object, the faster the beeping. | ESP32Servo, HC-SR04, ST7789, Wi-Fi web server |
| [`sonar_web_monitor`](sonar_web_monitor) | Distance monitor with an OLED bar graph, LCD status, LED/buzzer/relay alerts, and a radar-style web page where you can change the alert distance remotely | HC-SR04, SSD1306 + LCD on one I²C bus, web server |
| [`sonar_blynk_monitor`](sonar_blynk_monitor) | The same distance monitor connected to a Blynk phone dashboard (alarm switch, relay switch) | Blynk, BlynkTimer |
| [`weight_limit_scale`](weight_limit_scale) | Weight limit checker: shows weight on an OLED and triggers LED, buzzer and relay when it goes over 200 g | HX711, SSD1306 |
| [`bluetooth_control`](bluetooth_control) | Control an LED, buzzer and relay from a phone over Bluetooth Classic, while an OLED shows simulated temperature/humidity | BluetoothSerial, SSD1306 |
| [`buzzer_symphony`](buzzer_symphony) | Plays the opening of Beethoven's Symphony No. 5 on a buzzer, with a progress bar on an LCD | PWM tones, LCD I²C |
| [`led_chase`](led_chase) | LED chase effect (my first sketch). [Demo video](docs/led_chase_demo.mp4) | GPIO |
| [`serial_led_test`](serial_led_test) | Tests web-page-to-ESP32 serial communication by controlling the on-board LED | Serial |

## Running a project

1. Open the project folder in the **Arduino IDE** with the ESP32 board package installed.
2. Install the libraries listed at the top of the `.ino` file.
3. **Projects that use Wi-Fi or a cloud service** (`math_quest`, `dual_sense_bin`, `servo_sonar_radar`, `sonar_web_monitor`, `sonar_blynk_monitor`):
   copy `secrets.example.h` to **`secrets.h`** in the same folder and fill in your own Wi-Fi and token values.
   `secrets.h` is in `.gitignore`, so credentials are never committed.
4. Wiring for each project is described in the comment at the top of its `.ino` file.

## Skills practised

`C/C++ (Arduino)` · `ESP32 / ESP32-S3` · `I²C` · `SPI` · `UART` · `PWM` · `HX711 load cells` · `ultrasonic sensing` · `servo control` · `TFT/OLED/LCD graphics` · `Wi-Fi web servers` · `Bluetooth` · `MQTT` · `Blynk` · `ThingsBoard`
