#include <avr/wdt.h>
#include <TMCStepper.h>
#include <AccelStepper.h>
#include <FastLED.h>

//======================================================================================
// ENUMS FOR READABILITY
//======================================================================================
enum LightPattern {
  STANDBY = 0,
  IDLE = 1,
  MOVING = 2,
  PLAYER1_WIN = 3,
  PLAYER2_WIN = 4,
  RESETTING = 5
};

enum GameMode {
  MODE_STANDBY = 0,
  MODE_SINGLE_EASY = 1,
  MODE_TWO_PLAYER = 2,
  MODE_SINGLE_HARD = 3
};

enum TrackError {
  NO_ERROR = 0,
  STALL_DETECTED = 1,
  LIMIT_HIT = 2,
  BUSY = 3
};

//======================================================================================
// PIN DEFINITIONS
//======================================================================================
// Motor Controller 1
#define STEP_PIN1 3
#define DIR_PIN1 2
#define ENBL_PIN1 7
// STALL_PIN1 removed — stall detected via UART SG_RESULT

// Motor Controller 2
#define STEP_PIN2 9
#define DIR_PIN2 8
#define ENBL_PIN2 13
// STALL_PIN2 removed — stall detected via UART SG_RESULT

// Pressure Sensors
#define PRES1 A0
#define PRES2 A1

// Track 1 Limit Switches — now on digital pins
#define TRK1_LS1 22
#define TRK1_LS2 23

// Track 2 Limit Switches — now on digital pins
#define TRK2_LS1 24
#define TRK2_LS2 25

// Start Button — now on a digital pin with INPUT_PULLUP
#define STRT_PIN 26

// Status LEDs
#define START_LED 53
#define LED1 45
#define LED2 37

// LED Strip
#define DATA_PIN 27

//======================================================================================
// CONFIGURATION CONSTANTS
//======================================================================================
// Pressure Plate
const int PRESSURE_THRESHOLD = 500;       // Analog value below this = pressed

// LED Strip
const int NUM_LEDS = 30;

// TMC2209 Stepper Driver
const float TMC_R_SENSE = 0.11f;          // Sense resistor value
const int TMC_CURRENT_MA = 600;           // Motor current in mA
const int TMC_MICROSTEPS = 2;             // Microstepping setting

// Stall detection via SG_RESULT (stallGuard, read over UART)
// SG_RESULT is 10-bit (0–1023). Lower = more load / more likely stalled.
// 0 = definitely stalled. Tune SG_STALL_VALUE to your motor & load.
const uint16_t SG_STALL_VALUE = 50;       // SG_RESULT at or below this = stall
const int SG_MIN_SPEED = 300;             // Don't check stall below this step speed
                                           // (stallGuard is unreliable at very low speed)
const unsigned long SG_CHECK_INTERVAL_MS = 50; // How often to poll SG_RESULT (ms)

// Stepper Motion
const int MAX_STEPPER_SPEED = 2000;       // Steps per second
const int STEPPER_ACCELERATION = 1000;    // Steps per second²
const int NORMAL_SPEED = 1000;            // Normal movement speed
const int SLOW_SPEED = 400;               // Slow reset speed
const int BOUNDS_SPEED = 800;             // Speed during bounds finding
const int DRIFT_SPEED = 200;              // Speed for backward drift (Mode 3)

// Mode 3 Settings
const int DRIFT_INTERVAL_MS = 500;        // Check drift every 500ms
const int DRIFT_STEPS_PER_INTERVAL = 100; // Steps to drift back per interval

// Track Subdivisions
const int TRACK_SUBDIVISIONS = 5;         // Number of positions along track

//======================================================================================
// CUSTOM CLASSES
//======================================================================================

class MyLimitSwitch {
  public:
    MyLimitSwitch(int pinNum) {
      pin = pinNum;
      pinMode(pin, INPUT_PULLUP);
      previousState = digitalRead(pin);
    }

    // Returns true once on the falling edge (HIGH -> LOW transition)
    bool isPressed() {
      bool currentState = digitalRead(pin);
      bool pressed = (previousState == HIGH && currentState == LOW);
      previousState = currentState;
      return pressed;
    }

    // Returns true whenever the switch is currently held closed
    bool isCurrentlyPressed() {
      return digitalRead(pin) == LOW;
    }

  private:
    int pin;
    bool previousState;
};

// -----------------------------------------------------------------------
// MyButton — digital pin with INPUT_PULLUP.
// Wire button between STRT_PIN and GND.
// Open = HIGH, Pressed = LOW.
// -----------------------------------------------------------------------
class MyButton {
  public:
    MyButton(int pinNum) {
      pin = pinNum;
      pinMode(pin, INPUT_PULLUP);
      lastState = isPressed();
      pressStartTime = 0;
      holdTriggered = false;
    }

    bool isPressed() {
      return digitalRead(pin) == LOW;
    }

    // Returns true exactly once when the button has been held for `num` seconds.
    bool holdTimeOnce(int num) {
      unsigned long threshold = num * 1000UL;
      bool currentlyPressed = isPressed();

      if (currentlyPressed && !lastState) {
        // Rising edge — button just pressed
        pressStartTime = millis();
        holdTriggered = false;
      }
      else if (!currentlyPressed && lastState) {
        // Falling edge — button just released
        pressStartTime = 0;
        holdTriggered = false;
      }

      lastState = currentlyPressed;

      if (currentlyPressed && !holdTriggered && pressStartTime != 0) {
        if (millis() - pressStartTime >= threshold) {
          holdTriggered = true;
          return true;
        }
      }
      return false;
    }

  private:
    int pin;
    bool lastState;
    unsigned long pressStartTime;
    bool holdTriggered;
};

class MyLED {
  public:
    MyLED(int pinNum) {
      pin = pinNum;
      pinMode(pin, OUTPUT);
      state = false;
      digitalWrite(pin, LOW);
    }

    void turnOn() {
      digitalWrite(pin, HIGH);
      state = true;
    }

    void turnOff() {
      digitalWrite(pin, LOW);
      state = false;
    }

    void toggle() {
      if (state) {
        turnOff();
      } else {
        turnOn();
      }
    }

  private:
    int pin;
    bool state;
};

class MyLightstrip {
  public:
    MyLightstrip(int dataPin, int ledNum) {
      numLeds = ledNum;
      leds = new CRGB[numLeds];
      FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, numLeds);
      FastLED.setBrightness(255);
      FastLED.clear();
      FastLED.show();

      CurrPattern = STANDBY;
      resetPatternState();
    }

    ~MyLightstrip() {
      delete[] leds;
    }

    MyLightstrip(const MyLightstrip&) = delete;
    MyLightstrip& operator=(const MyLightstrip&) = delete;

    LightPattern getCurrentPattern() {
      return CurrPattern;
    }

    LightPattern play() {
      unsigned long now = millis();

      switch (CurrPattern) {
        case STANDBY:
          if (now - lastUpdate >= 250) {
            lastUpdate = now;
            if (patternIndex < numLeds) {
              leds[patternIndex] = CRGB::Cyan;
              patternIndex++;
            } else {
              FastLED.clear();
              patternIndex = 0;
            }
            FastLED.show();
          }
          break;

        case IDLE:
          if (now - lastUpdate >= 20) {
            lastUpdate = now;
            hue += 1;
            fill_solid(leds, numLeds, CHSV(hue, 255, 255));
            FastLED.show();
          }
          break;

        case MOVING:
          if (now - lastUpdate >= 100) {
            lastUpdate = now;
            FastLED.clear();
            leds[movingIndex] = CHSV(movingHue, 255, 255);
            movingHue += 32;
            movingIndex = (movingIndex + 1) % numLeds;
            FastLED.show();
          }
          break;

        case PLAYER1_WIN:
          if (now - lastUpdate >= 500) {
            lastUpdate = now;
            blinkState = !blinkState;
            if (blinkState) {
              fill_solid(leds, numLeds, CRGB::Green);
            } else {
              FastLED.clear();
            }
            FastLED.show();
          }
          break;

        case PLAYER2_WIN:
          if (now - lastUpdate >= 500) {
            lastUpdate = now;
            blinkState = !blinkState;
            if (blinkState) {
              fill_solid(leds, numLeds, CRGB::Blue);
            } else {
              FastLED.clear();
            }
            FastLED.show();
          }
          break;

        case RESETTING:
          if (now - lastUpdate >= 30) {
            lastUpdate = now;
            breathAngle = (breathAngle + 4) % 256;
            uint8_t brightness = sin8(breathAngle);
            for (int i = 0; i < numLeds; i++) {
              leds[i] = CRGB(brightness, 0, 0);
            }
            FastLED.show();
          }
          break;
      }

      return CurrPattern;
    }

    bool changePattern(LightPattern pattern) {
      if (pattern != CurrPattern) {
        CurrPattern = pattern;
        resetPatternState();
        return true;
      }
      return false;
    }

    void turnOff() {
      FastLED.clear();
      FastLED.show();
      resetPatternState();
    }

  private:
    int numLeds;
    CRGB* leds;
    LightPattern CurrPattern;

    unsigned long lastUpdate;
    int patternIndex;
    uint8_t hue;
    int movingIndex;
    uint8_t movingHue;
    bool blinkState;
    uint8_t breathAngle;

    void resetPatternState() {
      FastLED.clear();
      FastLED.show();
      lastUpdate = millis();
      patternIndex = 0;
      hue = 0;
      movingIndex = 0;
      movingHue = 0;
      blinkState = false;
      breathAngle = 0;
    }
};

class MyPressureplate {
  public:
    MyPressureplate(int pinNum) {
      pin = pinNum;
      pinMode(pin, INPUT);
    }

    int rawVal() {
      return analogRead(pin);
    }

    int currMass() {
      return 0;  // To be implemented with calibration data
    }

  private:
    int pin;
};

// -----------------------------------------------------------------------
// MyTrack — stall detection now uses SG_RESULT read over UART instead
// of the DIAG/stall pin. SG_RESULT is only reliable above a minimum
// motor speed; checks are gated on SG_MIN_SPEED and SG_CHECK_INTERVAL_MS.
// -----------------------------------------------------------------------
class MyTrack {
  public:
    MyTrack(int enPin, int stepPin, int dirPin, HardwareSerial* hwSerial,
            int startPin, int endPin)
      : startLS(startPin),
        endLS(endPin),
        driver(hwSerial, TMC_R_SENSE, 0),
        stepper(AccelStepper::DRIVER, stepPin, dirPin)
    {
      this->enPin = enPin;
      this->hwSerial = hwSerial;

      pinMode(enPin, OUTPUT);
      digitalWrite(enPin, HIGH);

      hwSerial->begin(115200);
      driver.begin();

      driver.toff(4);
      driver.rms_current(TMC_CURRENT_MA);
      driver.microsteps(TMC_MICROSTEPS);
      driver.en_spreadCycle(0);         // SpreadCycle off = StealthChop on
      driver.TCOOLTHRS(0xFFFFF);        // Keep CoolStep/stallGuard active across speed range
      driver.SGTHRS(0);                 // SGTHRS unused for DIAG; set 0
      driver.semin(0);                  // Disable CoolStep current scaling

      stepper.setMaxSpeed(MAX_STEPPER_SPEED);
      stepper.setAcceleration(STEPPER_ACCELERATION);
      stepper.setSpeed(NORMAL_SPEED);

      tracklength = 0;
      subdivs = TRACK_SUBDIVISIONS;
      stepSize = 0;

      resetting = false;
      findingBounds = false;
      slowResetActive = false;
      findBoundsState = 0;

      stallDetected = false;
      lastStallCheck = 0;
    }

    bool resetTrack() {
      if (startLS.isCurrentlyPressed()) {
        stepper.stop();
        stepper.setCurrentPosition(0);
        resetting = false;
        clearStall();
        digitalWrite(enPin, HIGH);
        return true;
      }

      if (resetting) {
        if (startLS.isPressed() || startLS.isCurrentlyPressed()) {
          stepper.stop();
          stepper.setCurrentPosition(0);
          resetting = false;
          clearStall();
          digitalWrite(enPin, HIGH);
          return true;
        }
        return false;
      } else {
        if (!findingBounds && !slowResetActive && !stepper.isRunning()) {
          resetting = true;
          clearStall();
          digitalWrite(enPin, LOW);
          stepper.moveTo(-1000000);
          stepper.setSpeed(NORMAL_SPEED);
          stepper.setAcceleration(STEPPER_ACCELERATION);
          return false;
        }
        return false;
      }
    }

    void findBounds() {
      if (resetting || findingBounds || slowResetActive || stepper.isRunning()) return;

      findingBounds = true;
      clearStall();

      if (startLS.isCurrentlyPressed()) {
        stepper.setCurrentPosition(0);
        findBoundsState = 2;
        stepper.moveTo(1000000);
        stepper.setSpeed(BOUNDS_SPEED);
        digitalWrite(enPin, LOW);
      } else {
        findBoundsState = 1;
        stepper.moveTo(-1000000);
        stepper.setSpeed(BOUNDS_SPEED);
        digitalWrite(enPin, LOW);
      }
    }

    TrackError moveNextPosition() {
      if (tracklength == 0) return BUSY;
      if (resetting || findingBounds) return BUSY;
      if (stallDetected) return STALL_DETECTED;

      if (slowResetActive) {
        slowResetActive = false;
      }

      if (stepSize == 0) {
        stepSize = tracklength / subdivs;
      }

      long target = stepper.currentPosition() + stepSize;
      stepper.moveTo(target);
      digitalWrite(enPin, LOW);

      clearStall();
      return NO_ERROR;
    }

    void slowReset() {
      if (resetting || findingBounds) return;
      if (stallDetected) return;

      if (!slowResetActive && !stepper.isRunning()) {
        slowResetActive = true;
        clearStall();
        digitalWrite(enPin, LOW);
        stepper.moveTo(-1000000);
        stepper.setSpeed(SLOW_SPEED);
        stepper.setAcceleration(300);
      }
    }

    bool isStalled() {
      return stallDetected;
    }

    void clearStall() {
      stallDetected = false;
    }

    void run() {
      updateStepper();
      checkStallDetection();
      handleLimitSwitches();
      updateDriverState();
    }

    bool isFindingBounds() {
      return findingBounds;
    }

    bool isAtStart() {
      return startLS.isCurrentlyPressed();
    }

    bool isAtEnd() {
      return endLS.isCurrentlyPressed();
    }

    bool isMoving() {
      return stepper.isRunning();
    }

    long getCurrentPosition() {
      return stepper.currentPosition();
    }

    void driftBack(int steps, int speed) {
      if (resetting || findingBounds || stallDetected) return;
      if (startLS.isCurrentlyPressed()) return;

      if (slowResetActive) {
        slowResetActive = false;
      }

      long currentPos = stepper.currentPosition();
      long targetPos = currentPos - steps;
      if (targetPos < 0) targetPos = 0;

      if (targetPos != currentPos) {
        stepper.moveTo(targetPos);
        stepper.setSpeed(speed);
        digitalWrite(enPin, LOW);
      }
    }

    void moveToPosition(long targetPos, int speed) {
      if (resetting || findingBounds || stallDetected) return;

      if (targetPos < 0) targetPos = 0;
      if (tracklength > 0 && targetPos > tracklength) targetPos = tracklength;

      if (slowResetActive) {
        slowResetActive = false;
      }

      stepper.moveTo(targetPos);
      stepper.setSpeed(speed);
      digitalWrite(enPin, LOW);
    }

  private:
    int enPin;
    HardwareSerial* hwSerial;
    AccelStepper stepper;

    MyLimitSwitch startLS;
    MyLimitSwitch endLS;

    TMC2209Stepper driver;

    long tracklength;
    int subdivs;
    long stepSize;

    bool resetting;
    bool findingBounds;
    int findBoundsState;
    bool slowResetActive;

    bool stallDetected;
    unsigned long lastStallCheck;

    void updateStepper() {
      stepper.run();
    }

    // -----------------------------------------------------------------------
    // Stall detection via UART — reads SG_RESULT from the TMC2209.
    // SG_RESULT is 10-bit: 0 = maximum load / stall, 1023 = no load.
    // Only checked when the motor is running above SG_MIN_SPEED to avoid
    // false positives during acceleration from rest.
    // -----------------------------------------------------------------------
    void checkStallDetection() {
      if (!stepper.isRunning()) return;
      if (abs(stepper.speed()) < SG_MIN_SPEED) return;
      if (millis() - lastStallCheck < SG_CHECK_INTERVAL_MS) return;

      lastStallCheck = millis();

      uint16_t sg = driver.SG_RESULT();

      if (sg <= SG_STALL_VALUE) {
        if (!stallDetected) {
          stallDetected = true;
          stepper.stop();
          digitalWrite(enPin, HIGH);
          Serial.print("Stall detected via SG_RESULT: ");
          Serial.println(sg);
        }
      }
    }

    void handleLimitSwitches() {
      if (resetting) {
        if (startLS.isPressed() || startLS.isCurrentlyPressed()) {
          stepper.stop();
          stepper.setCurrentPosition(0);
          resetting = false;
          clearStall();
          digitalWrite(enPin, HIGH);
        }
      }
      else if (findingBounds) {
        if (findBoundsState == 1) {
          if (startLS.isPressed() || startLS.isCurrentlyPressed()) {
            stepper.stop();
            stepper.setCurrentPosition(0);
            findBoundsState = 2;
            stepper.moveTo(1000000);
          }
        } else if (findBoundsState == 2) {
          if (endLS.isPressed() || endLS.isCurrentlyPressed()) {
            stepper.stop();
            tracklength = stepper.currentPosition();
            stepSize = tracklength / subdivs;
            findBoundsState = 0;
            findingBounds = false;
            clearStall();
            digitalWrite(enPin, HIGH);

            Serial.print("Track bounds found: ");
            Serial.print(tracklength);
            Serial.println(" steps");
          }
        }

        if (stallDetected) {
          Serial.println("Stall during bounds finding - aborting");
          stepper.stop();
          findingBounds = false;
          digitalWrite(enPin, HIGH);
        }
      }
      else if (slowResetActive) {
        if (startLS.isPressed() || startLS.isCurrentlyPressed()) {
          stepper.stop();
          stepper.setCurrentPosition(0);
          slowResetActive = false;
          clearStall();
          digitalWrite(enPin, HIGH);
        }

        if (stallDetected) {
          Serial.println("Stall during slow reset - aborting");
          stepper.stop();
          slowResetActive = false;
          digitalWrite(enPin, HIGH);
        }
      }
      else {
        if (startLS.isCurrentlyPressed() || endLS.isCurrentlyPressed()) {
          stepper.stop();
          digitalWrite(enPin, HIGH);
        }
      }
    }

    void updateDriverState() {
      if (!stepper.isRunning() && digitalRead(enPin) == LOW) {
        digitalWrite(enPin, HIGH);
      }
    }
};

//======================================================================================
// MODE STATE STRUCTS
//======================================================================================
struct Mode1State {
  bool gameWon = false;
  bool waitingForReset = false;
  bool isResetting = false;
  bool isMoving = false;
  bool lastPlateState = false;

  void reset() {
    gameWon = false;
    waitingForReset = false;
    isResetting = false;
    isMoving = false;
    lastPlateState = false;
  }
};

struct Mode2State {
  bool gameWon = false;
  int winner = 0;
  bool waitingForReset = false;
  bool isResetting = false;
  bool track1Moving = false;
  bool track2Moving = false;
  bool lastPlate1State = false;
  bool lastPlate2State = false;

  void reset() {
    gameWon = false;
    winner = 0;
    waitingForReset = false;
    isResetting = false;
    track1Moving = false;
    track2Moving = false;
    lastPlate1State = false;
    lastPlate2State = false;
  }
};

struct Mode3State {
  bool gameWon = false;
  bool waitingForReset = false;
  bool isResetting = false;
  bool isMovingForward = false;
  bool isDrifting = false;
  unsigned long lastDriftTime = 0;
  bool lastPlateState = false;

  void reset() {
    gameWon = false;
    waitingForReset = false;
    isResetting = false;
    isMovingForward = false;
    isDrifting = false;
    lastDriftTime = 0;
    lastPlateState = false;
  }
};

//======================================================================================
// GLOBAL VARIABLES
//======================================================================================
bool gameOn = false;
GameMode currentMode = MODE_STANDBY;
bool modeJustChanged = false;

//======================================================================================
// OBJECT INSTANCES
//======================================================================================
MyButton startButton(STRT_PIN);
MyLED statusLED(START_LED);
MyLED led1(LED1);
MyLED led2(LED2);
MyLightstrip lightStrip(DATA_PIN, NUM_LEDS);
MyPressureplate pressurePlate1(PRES1);
MyPressureplate pressurePlate2(PRES2);

// Note: no stallPin argument — stall is detected via UART SG_RESULT
MyTrack track1(ENBL_PIN1, STEP_PIN1, DIR_PIN1, &Serial1, TRK1_LS1, TRK1_LS2);
MyTrack track2(ENBL_PIN2, STEP_PIN2, DIR_PIN2, &Serial2, TRK2_LS1, TRK2_LS2);

//======================================================================================
// HELPER FUNCTIONS
//======================================================================================
void updateModeLEDs() {
  switch (currentMode) {
    case MODE_STANDBY:
      led1.turnOff();
      led2.turnOff();
      break;
    case MODE_SINGLE_EASY:
      led1.turnOn();
      led2.turnOff();
      break;
    case MODE_TWO_PLAYER:
      led1.turnOff();
      led2.turnOn();
      break;
    case MODE_SINGLE_HARD:
      led1.turnOn();
      led2.turnOn();
      break;
  }
}

void setGameOn(bool on) {
  if (on == gameOn) return;

  gameOn = on;

  if (on) {
    statusLED.turnOn();
    updateModeLEDs();
  } else {
    statusLED.turnOff();
    led1.turnOff();
    led2.turnOff();
    lightStrip.turnOff();
    digitalWrite(ENBL_PIN1, HIGH);
    digitalWrite(ENBL_PIN2, HIGH);
  }
}

void processSerialCommands() {
  if (Serial.available()) {
    char cmd = Serial.read();
    switch(cmd) {
      case '1':
        Serial.print("Track 1 Position: ");
        Serial.println(track1.getCurrentPosition());
        break;
      case '2':
        Serial.print("Track 2 Position: ");
        Serial.println(track2.getCurrentPosition());
        break;
      case 'p':
        Serial.print("Pressure 1: ");
        Serial.print(pressurePlate1.rawVal());
        Serial.print(" | Pressure 2: ");
        Serial.println(pressurePlate2.rawVal());
        break;
      case 'b':
        Serial.print("Button state: ");
        Serial.println(digitalRead(STRT_PIN) == LOW ? "PRESSED" : "OPEN");
        break;
      case 'm':
        Serial.print("Current Mode: ");
        Serial.println(currentMode);
        break;
      case 'h':
        Serial.println("Commands: 1=Track1Pos 2=Track2Pos p=Pressure b=Button m=Mode h=Help");
        break;
    }
  }
}

//======================================================================================
// MODE FUNCTIONS  (unchanged from original)
//======================================================================================
void runMode0() {
  if (modeJustChanged) {
    lightStrip.changePattern(STANDBY);
  }
}

void runMode1() {
  static Mode1State state;

  if (modeJustChanged) {
    state.reset();
    lightStrip.changePattern(IDLE);

    if (!track1.isAtStart()) {
      track1.resetTrack();
    }

    Serial.println("Mode 1: Single Player Easy");
  }

  if (state.gameWon) {
    lightStrip.changePattern(PLAYER1_WIN);

    if (startButton.holdTimeOnce(0)) {
      state.waitingForReset = true;
    }

    if (state.waitingForReset && !state.isResetting) {
      lightStrip.changePattern(RESETTING);
      track1.resetTrack();
      state.isResetting = true;
    }

    if (state.isResetting) {
      if (track1.isAtStart()) {
        state.reset();
        lightStrip.changePattern(IDLE);
        Serial.println("Ready to play again");
      }
    }

    return;
  }

  int pressureVal = pressurePlate1.rawVal();
  bool platePressed = (pressureVal < PRESSURE_THRESHOLD);

  if (state.isMoving) {
    lightStrip.changePattern(MOVING);

    if (!track1.isMoving()) {
      state.isMoving = false;
      lightStrip.changePattern(IDLE);

      if (track1.isAtEnd()) {
        state.gameWon = true;
        Serial.println("PLAYER WINS!");
      }
    }
  }
  else {
    lightStrip.changePattern(IDLE);

    if (!platePressed && state.lastPlateState) {
      if (!track1.isAtEnd()) {
        track1.moveNextPosition();
        state.isMoving = true;
        Serial.println("Step detected");
      }
    }

    state.lastPlateState = platePressed;
  }
}

void runMode2() {
  static Mode2State state;

  if (modeJustChanged) {
    state.reset();
    lightStrip.changePattern(IDLE);

    if (!track1.isAtStart()) {
      track1.resetTrack();
    }
    if (!track2.isAtStart()) {
      track2.resetTrack();
    }

    Serial.println("Mode 2: Two Player Competitive");
  }

  if (state.gameWon) {
    if (state.winner == 1) {
      lightStrip.changePattern(PLAYER1_WIN);
    } else if (state.winner == 2) {
      lightStrip.changePattern(PLAYER2_WIN);
    }

    if (startButton.holdTimeOnce(0)) {
      state.waitingForReset = true;
    }

    if (state.waitingForReset && !state.isResetting) {
      lightStrip.changePattern(RESETTING);
      track1.resetTrack();
      track2.resetTrack();
      state.isResetting = true;
    }

    if (state.isResetting) {
      if (track1.isAtStart() && track2.isAtStart()) {
        state.reset();
        lightStrip.changePattern(IDLE);
        Serial.println("Ready to play again");
      }
    }

    return;
  }

  int pressureVal1 = pressurePlate1.rawVal();
  int pressureVal2 = pressurePlate2.rawVal();
  bool plate1Pressed = (pressureVal1 < PRESSURE_THRESHOLD);
  bool plate2Pressed = (pressureVal2 < PRESSURE_THRESHOLD);

  if (state.track1Moving) {
    if (!track1.isMoving()) {
      state.track1Moving = false;

      if (track1.isAtEnd()) {
        state.gameWon = true;
        state.winner = 1;
        Serial.println("PLAYER 1 WINS!");
        return;
      }
    }
  }
  else if (!state.gameWon) {
    if (!plate1Pressed && state.lastPlate1State) {
      if (!track1.isAtEnd()) {
        track1.moveNextPosition();
        state.track1Moving = true;
        Serial.println("Player 1 step");
      }
    }
  }
  state.lastPlate1State = plate1Pressed;

  if (state.track2Moving) {
    if (!track2.isMoving()) {
      state.track2Moving = false;

      if (track2.isAtEnd()) {
        state.gameWon = true;
        state.winner = 2;
        Serial.println("PLAYER 2 WINS!");
        return;
      }
    }
  }
  else if (!state.gameWon) {
    if (!plate2Pressed && state.lastPlate2State) {
      if (!track2.isAtEnd()) {
        track2.moveNextPosition();
        state.track2Moving = true;
        Serial.println("Player 2 step");
      }
    }
  }
  state.lastPlate2State = plate2Pressed;

  if (state.track1Moving || state.track2Moving) {
    lightStrip.changePattern(MOVING);
  } else {
    lightStrip.changePattern(IDLE);
  }
}

void runMode3() {
  static Mode3State state;

  if (modeJustChanged) {
    state.reset();
    lightStrip.changePattern(IDLE);

    if (!track1.isAtStart()) {
      track1.resetTrack();
    }

    Serial.println("Mode 3: Hard Mode - Track drifts back!");
  }

  if (state.gameWon) {
    lightStrip.changePattern(IDLE);

    if (startButton.holdTimeOnce(0)) {
      state.waitingForReset = true;
    }

    if (state.waitingForReset && !state.isResetting) {
      lightStrip.changePattern(RESETTING);
      track1.resetTrack();
      state.isResetting = true;
    }

    if (state.isResetting) {
      if (track1.isAtStart()) {
        state.reset();
        lightStrip.changePattern(IDLE);
        Serial.println("Ready to play again");
      }
    }

    return;
  }

  int pressureVal = pressurePlate1.rawVal();
  bool platePressed = (pressureVal < PRESSURE_THRESHOLD);

  if (platePressed && !state.lastPlateState && !state.gameWon) {
    if (!track1.isAtEnd()) {
      track1.moveNextPosition();
      state.isMovingForward = true;
      state.isDrifting = false;
      Serial.println("Forward move");
    }
  }
  state.lastPlateState = platePressed;

  if (state.isMovingForward && !track1.isMoving()) {
    state.isMovingForward = false;

    if (track1.isAtEnd()) {
      state.gameWon = true;
      Serial.println("Reached the end!");
      return;
    }

    if (!track1.isAtStart()) {
      state.isDrifting = true;
      state.lastDriftTime = millis();
      Serial.println("Starting drift");
    }
  }

  if (state.isDrifting && !state.gameWon) {
    if (track1.isAtStart()) {
      state.isDrifting = false;
      Serial.println("At start - drift stopped");
    }
    else if (millis() - state.lastDriftTime >= DRIFT_INTERVAL_MS) {
      state.lastDriftTime = millis();

      if (!track1.isMoving()) {
        track1.driftBack(DRIFT_STEPS_PER_INTERVAL, DRIFT_SPEED);
      }
    }
  }

  if (state.isMovingForward) {
    lightStrip.changePattern(MOVING);
  } else {
    lightStrip.changePattern(IDLE);
  }
}

//======================================================================================
// SETUP
//======================================================================================
void setup() {
  wdt_enable(WDTO_2S);  // Enable 2-second watchdog timer

  Serial.begin(9600);

  setGameOn(false);
  delay(500);

  Serial.println("=== CALIBRATION MODE ===");
  Serial.println("Press button to find track bounds");
  Serial.println("(Button on pin 26 with INPUT_PULLUP — wire button to GND)");

  for (int i = 0; i < 2; i++) {
    statusLED.turnOn();
    delay(300);
    statusLED.turnOff();
    delay(300);
  }

  Serial.println("Waiting for button press...");

  while (!startButton.isPressed()) {
    wdt_reset();
    delay(50);
  }

  delay(200);
  while (startButton.isPressed()) {
    wdt_reset();
    delay(10);
  }

  Serial.println("Finding track bounds...");

  track1.findBounds();
  track2.findBounds();

  while (track1.isFindingBounds() || track2.isFindingBounds()) {
    wdt_reset();
    track1.run();
    track2.run();
    delay(10);
  }

  Serial.println("Returning to start...");

  track1.resetTrack();
  track2.resetTrack();

  while (!track1.isAtStart() || !track2.isAtStart()) {
    wdt_reset();
    track1.run();
    track2.run();
    delay(10);
  }

  statusLED.turnOn();
  Serial.println("=== CALIBRATION COMPLETE ===");
  Serial.println("System ready. Press button to start game.");
  Serial.println("Type 'h' for help with debug commands.");
}

//======================================================================================
// MAIN LOOP
//======================================================================================
void loop() {
  wdt_reset();  // Pet the watchdog

  processSerialCommands();

  if (!gameOn) {
    if (startButton.holdTimeOnce(0)) {
      setGameOn(true);
      Serial.println("Game ON");
    }
    return;
  }

  track1.run();
  track2.run();
  lightStrip.play();

  if (startButton.holdTimeOnce(2)) {
    setGameOn(false);
    Serial.println("Game OFF");
    return;
  }

  if (startButton.holdTimeOnce(1)) {
    currentMode = (GameMode)((currentMode + 1) % 4);
    modeJustChanged = true;
    Serial.print("Mode changed to: ");
    Serial.println(currentMode);

    updateModeLEDs();
    track1.resetTrack();
    track2.resetTrack();
  }

  switch (currentMode) {
    case MODE_STANDBY: runMode0(); break;
    case MODE_SINGLE_EASY: runMode1(); break;
    case MODE_TWO_PLAYER: runMode2(); break;
    case MODE_SINGLE_HARD: runMode3(); break;
  }

  modeJustChanged = false;
}
