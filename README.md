# RoboRumble
Firmware and Bluetooth control page for UCD Elecsoc's RoboRumble robotic car event.

## Overview
I wrote the firmware and browser controller for UCD Elecsoc's RoboRumble, a robotic car event. Each robot runs an ESP32-C6 that advertises itself over Bluetooth Low Energy. Contestants open a web page in Chrome on their laptop, connect straight to their robot (no app, server or router needed), and drive it with the keyboard.

## Features
- **Direct Bluetooth Low Energy control**: Chrome's Web Bluetooth API connects the laptop to the robot. No WiFi, server or phone needed.
- **Remappable keys**: WASD by default. Click any binding to change it, and conflicting keys are swapped automatically. Bindings are saved in the browser.
- **Custom drive equations**: edit the left and right motor equations (e.g. `V + H`, `V - 0.5*H`) live in the browser to change how the robot steers. A small hand-written parser evaluates them, with no `eval()`, and an invalid formula never reaches the motors.
- **Soft-start ramp**: motor speed ramps toward the target (15 PWM steps every 10 ms) instead of jumping to full power, which reduces inrush current and brown-outs.
- **Safety watchdog**: the motors stop if the robot hears nothing for 400 ms, if the Bluetooth link drops, if the browser tab loses focus, or when STOP is pressed.
- **Live readout**: the page shows each motor's direction and power percentage.

## Hardware
- ESP32-C6 DevKit
- L298N motor driver
- 2 × DC motors
- Separate battery pack for the motors

<!-- Add a photo of the robot here, e.g. ![RoboRumble robot](docs/robot.jpg) -->

## How it Works
The ESP32-C6 only supports Bluetooth Low Energy (not Classic), so the firmware uses the [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) library to expose a single writable GATT characteristic, using the Nordic UART service UUIDs.

1. The browser reads which keys are held and turns them into two values: **V** (forward = +1, backward = −1) and **H** (right = +1, left = −1).
2. It evaluates the left and right drive equations, clamps each result to ±1 and scales it to ±255.
3. It sends `M:<left>,<right>` (e.g. `M:255,-255`) to the robot every 150 ms while a key is held, and immediately on every key change.
4. The ESP32 parses the command, sets the motor targets, and ramps each L298N channel toward its target using PWM on ENA/ENB and direction on IN1–IN4.

The 150 ms resend keeps the 400 ms watchdog happy while a key is held. If anything goes wrong, such as a closed tab, a sleeping laptop or a dropped link, the robot stops on its own.

## Wiring
| L298N pin | ESP32 GPIO |
|---|---|
| ENA (left motor PWM) | 6 |
| IN1 | 7 |
| IN2 | 10 |
| ENB (right motor PWM) | 20 |
| IN3 | 22 |
| IN4 | 21 |

| Connection | |
|---|---|
| OUT1 / OUT2 → left motor | |
| OUT3 / OUT4 → right motor | |
| Battery → L298N 12V / GND | Motor power |
| L298N GND ↔ ESP32 GND | Common ground |
| ENA / ENB jumper caps OFF | So the ESP32 can PWM them |

If starting the motors browns out the ESP32, add a 470–1000 µF bulk capacitor across the L298N's power input terminals.

## Getting Started
1. **Install the board**: Arduino IDE → Boards Manager → "esp32" by Espressif (3.x). Select **ESP32C6 Dev Module**.
2. **Add the library**: Library Manager → **NimBLE-Arduino** by h2zero, version 2.x.
3. **Name your robot**: open [`firmware/esp32_ble_wasd/esp32_ble_wasd.ino`](firmware/esp32_ble_wasd/esp32_ble_wasd.ino) and change `DEVICE_NAME` so every robot is unique.
4. **Upload** and open the Serial Monitor at 115200 baud. You should see `[BLE] advertising as <DEVICE_NAME>`.

## Using the Controls
1. Open [`controller/control.html`](controller/control.html) in **Chrome or Edge on desktop**. Web Bluetooth isn't supported in Safari or on iOS.
2. Click **Connect via Bluetooth** and choose your robot from the popup.
3. Hold **W/A/S/D** to drive. Combine two keys (e.g. W + D) for a turning drive.

| Default key | Action |
|---|---|
| W | Forward |
| S | Backward |
| A | Turn left |
| D | Turn right |
| STOP button | Stop both motors |

## Key Challenges
The main challenge was power. When a motor started at full power, its inrush current sagged the supply enough to brown out and reset the ESP32 mid-match. I added a soft-start ramp in the firmware so the motors build up speed over about 170 ms. Safety was the other priority: a robot that keeps driving after its controller disconnects is dangerous at a live event. So I added several stop conditions: a stop on disconnect, and a stop when the browser loses focus.

## Licence
MIT. See [LICENSE](LICENSE).
