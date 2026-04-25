# Esp-32-c3-mini-DOOM-engine
Its a classic DOOM game implemented on an Esp 32 c3 mini

For the buttons
| Button Side | Connection                |
| ----------- | ------------------------- |
| One leg     | GPIO pin (2 / 3 / 4 / 10) |
| Other leg   | GND                       |


| Component    | Pin     |
| ------------ | ------- |
| OLED VCC     | 3.3V    |
| OLED GND     | GND     |
| OLED SDA     | GPIO 5  |
| OLED SCL     | GPIO 6  |
| Button UP    | GPIO 2  |
| Button DOWN  | GPIO 3  |
| Button LEFT  | GPIO 4  |
| Button RIGHT | GPIO 10 |

| Battery   | TP4056 Pin |
| --------- | ---------- |
| + (red)   | B+         |
| – (black) | B-         |

| TP4056 | ESP32-C3 |
| ------ | -------- |
| OUT+   | 5V / VIN |
| OUT-   | GND      |
