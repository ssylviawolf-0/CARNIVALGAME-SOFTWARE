# Carnival Game Firmware

Arduino Mega 2560 firmware for a physical carnival game with stepper-motor-driven tracks, pressure plate inputs, LED feedback, and multiple game modes.

## Sketches

| File | Description |
|---|---|
| `main.ino` | Production firmware |
| `main_dev.ino` | Development firmware with debounce, calibration timeout, and grace period improvements |
| `test_flex_sensor.ino` | Flex/force sensor test utility |
| `test_sound_module.ino` | DFPlayer Mini sound module test utility |

Older sketches and earlier iterations are in the `archive/` folder.

## Required Libraries

Install via Arduino Library Manager:

- **TMCStepper** — TMC2209 UART stepper driver control
- **AccelStepper** — stepper motion planning
- **FastLED** — WS2812B LED strip
- **DFRobotDFPlayerMini** — DFPlayer Mini sound module

## Game Modes

- **Mode 0 (Standby)** — idle light pattern, no motor activity
- **Mode 1 (Single Easy)** — pressure plate release advances track one subdivision
- **Mode 2 (Two Player)** — competitive race on both tracks
- **Mode 3 (Single Hard)** — like Mode 1 but track drifts backward between presses

## Upload

1. Open the desired `.ino` sketch in Arduino IDE
2. Select board **Arduino Mega 2560**
3. Upload via USB
