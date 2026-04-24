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
  LIMIT_HIT = 2,
  BUSY = 3
};

//======================================================================================
// PIN DEFINITIONS
//======================================================================================
#define STEP_PIN1 3
#define DIR_PIN1 2
#define ENBL_PIN1 7

#define STEP_PIN2 9
#define DIR_PIN2 8
#define ENBL_PIN2 13

#define PRES1 A0
#define PRES2 A1

#define TRK1_LS1 25
#define TRK1_LS2 23
#define TRK2_LS1 29
#define TRK2_LS2 27

#define STRT_PIN 48

#define START_LED 53
#define LED1 45
#define LED2 37

#define DATA_PIN 6

//======================================================================================
// CONFIGURATION CONSTANTS
//======================================================================================
const int PRESSURE_THRESHOLD = 500;
const int NUM_LEDS = 40;
const float TMC_R_SENSE = 0.11f;
const int TMC_CURRENT_MA = 600;
const int TMC_MICROSTEPS = 2;
const int MAX_STEPPER_SPEED = 2000;
const int STEPPER_ACCELERATION = 1000;
const int NORMAL_SPEED = 1000;
const int SLOW_SPEED = 400;
const int BOUNDS_SPEED = 800;
const int DRIFT_SPEED = 200;
const int DRIFT_INTERVAL_MS = 500;
const int DRIFT_STEPS_PER_INTERVAL = 100;
const int TRACK_SUBDIVISIONS = 5;
const unsigned long DRIFT_GRACE_PERIOD_MS = 1000;
const unsigned long CALIBRATION_TIMEOUT_MS = 15000;
const unsigned long PLATE_DEBOUNCE_MS = 50;
const unsigned long PLATE_MIN_PRESS_MS = 250;

//======================================================================================
// CUSTOM CLASSES
//======================================================================================

class MyLimitSwitch {
  public:
    MyLimitSwitch(int pinNum) {
      pin = pinNum;
      previousState = HIGH;
    }

    void reinit() {
      pinMode(pin, INPUT_PULLUP);
      delay(50);
      previousState = digitalRead(pin);
    }

    bool isPressed() {
      bool currentState = digitalRead(pin);
      bool pressed = (previousState == HIGH && currentState == LOW);
      previousState = currentState;
      return pressed;
    }

    bool isCurrentlyPressed() {
      return digitalRead(pin) == LOW;
    }

  private:
    int pin;
    bool previousState;
};

class MyButton {
  public:
    MyButton(int pinNum) {
      pin = pinNum;
      lastState = false;
      pressStartTime = 0;
      holdTriggered = false;
    }

    void begin() {
      pinMode(pin, INPUT_PULLUP);
      delay(50);
      lastState = isPressed();
    }

    bool isPressed() {
      return digitalRead(pin) == LOW;
    }

    bool holdTimeOnce(int num) {
      unsigned long threshold = num * 1000UL;
      bool currentlyPressed = isPressed();

      if (currentlyPressed && !lastState) {
        pressStartTime = millis();
        holdTriggered = false;
      }
      else if (!currentlyPressed && lastState) {
        if (num == 0 && !holdTriggered && pressStartTime != 0) {
          pressStartTime = 0;
          lastState = currentlyPressed;
          return true;
        }
        pressStartTime = 0;
        holdTriggered = false;
      }
      lastState = currentlyPressed;

      if (num > 0 && currentlyPressed && !holdTriggered && pressStartTime != 0) {
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
      state = false;
    }

    void begin() {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW);
    }

    void turnOn()  { digitalWrite(pin, HIGH); state = true; }
    void turnOff() { digitalWrite(pin, LOW);  state = false; }
    void toggle()  { if (state) turnOff(); else turnOn(); }

  private:
    int pin;
    bool state;
};

CRGB leds[30];

class MyLightstrip {
  public:
    MyLightstrip() {
      CurrPattern = STANDBY;
      lastUpdate = 0;
      patternIndex = 0;
      hue = 0;
      movingIndex = 0;
      movingHue = 0;
      blinkState = false;
      breathAngle = 0;
    }

    void begin() {
      FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
      FastLED.setBrightness(255);
      FastLED.clear();
      FastLED.show();
    }

    LightPattern getCurrentPattern() { return CurrPattern; }

    void play() {
      unsigned long now = millis();
      switch (CurrPattern) {
        case STANDBY:
          if (now - lastUpdate >= 250) {
            lastUpdate = now;
            if (patternIndex < NUM_LEDS) { leds[patternIndex] = CRGB::Cyan; patternIndex++; }
            else { FastLED.clear(); patternIndex = 0; }
            FastLED.show();
          }
          break;
        case IDLE:
          if (now - lastUpdate >= 20) {
            lastUpdate = now;
            hue += 1;
            fill_solid(leds, NUM_LEDS, CHSV(hue, 255, 255));
            FastLED.show();
          }
          break;
        case MOVING:
          if (now - lastUpdate >= 100) {
            lastUpdate = now;
            FastLED.clear();
            leds[movingIndex] = CHSV(movingHue, 255, 255);
            movingHue += 32;
            movingIndex = (movingIndex + 1) % NUM_LEDS;
            FastLED.show();
          }
          break;
        case PLAYER1_WIN:
          if (now - lastUpdate >= 500) {
            lastUpdate = now;
            blinkState = !blinkState;
            if (blinkState) fill_solid(leds, NUM_LEDS, CRGB::Green);
            else FastLED.clear();
            FastLED.show();
          }
          break;
        case PLAYER2_WIN:
          if (now - lastUpdate >= 500) {
            lastUpdate = now;
            blinkState = !blinkState;
            if (blinkState) fill_solid(leds, NUM_LEDS, CRGB::Blue);
            else FastLED.clear();
            FastLED.show();
          }
          break;
        case RESETTING:
          if (now - lastUpdate >= 30) {
            lastUpdate = now;
            breathAngle = (breathAngle + 4) % 256;
            uint8_t brightness = sin8(breathAngle);
            for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(brightness, 0, 0);
            FastLED.show();
          }
          break;
      }
    }

    bool changePattern(LightPattern pattern) {
      if (pattern != CurrPattern) {
        CurrPattern = pattern;
        FastLED.clear();
        FastLED.show();
        lastUpdate = millis();
        patternIndex = 0;
        hue = 0;
        movingIndex = 0;
        movingHue = 0;
        blinkState = false;
        breathAngle = 0;
        return true;
      }
      return false;
    }

    void turnOff() {
      FastLED.clear();
      FastLED.show();
      CurrPattern = STANDBY;
    }

  private:
    LightPattern CurrPattern;
    unsigned long lastUpdate;
    int patternIndex;
    uint8_t hue;
    int movingIndex;
    uint8_t movingHue;
    bool blinkState;
    uint8_t breathAngle;
};

class MyPressureplate {
  public:
    MyPressureplate(int pinNum) {
      pin = pinNum;
      debounceStart = 0;
      pressStart = 0;
      debouncedState = false;
      rawPressedState = false;
      stepReady = false;
      stepConsumed = false;
    }

    void begin() {
      pinMode(pin, INPUT);
    }

    int rawVal() { return analogRead(pin); }

    void update() {
      bool rawPressed = (analogRead(pin) < PRESSURE_THRESHOLD);

      if (rawPressed != rawPressedState) {
        rawPressedState = rawPressed;
        debounceStart = millis();
      }

      if (millis() - debounceStart >= PLATE_DEBOUNCE_MS) {
        bool prevDebounced = debouncedState;
        debouncedState = rawPressedState;

        if (debouncedState && !prevDebounced) {
          pressStart = millis();
          stepConsumed = false;
          stepReady = false;
        }

        if (!debouncedState && prevDebounced) {
          pressStart = 0;
          stepReady = false;
          stepConsumed = false;
        }
      }

      if (debouncedState && !stepConsumed && pressStart != 0) {
        if (millis() - pressStart >= PLATE_MIN_PRESS_MS) {
          stepReady = true;
        }
      }
    }

    bool stepTriggered() {
      if (stepReady && !stepConsumed) {
        stepConsumed = true;
        stepReady = false;
        return true;
      }
      return false;
    }

    bool isPressed() { return debouncedState; }

  private:
    int pin;
    unsigned long debounceStart;
    unsigned long pressStart;
    bool rawPressedState;
    bool debouncedState;
    bool stepReady;
    bool stepConsumed;
};

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
      tracklength = 0;
      subdivs = TRACK_SUBDIVISIONS;
      stepSize = 0;
      resetting = false;
      findingBounds = false;
      slowResetActive = false;
      findBoundsState = 0;
      atEnd = false;
    }

    void begin() {
      pinMode(enPin, OUTPUT);
      digitalWrite(enPin, HIGH);

      startLS.reinit();
      endLS.reinit();

      hwSerial->begin(115200);
      driver.begin();
      driver.toff(4);
      driver.rms_current(TMC_CURRENT_MA);
      driver.microsteps(TMC_MICROSTEPS);
      driver.en_spreadCycle(0);
      driver.TCOOLTHRS(0);
      driver.SGTHRS(0);
      stepper.setMaxSpeed(MAX_STEPPER_SPEED);
      stepper.setAcceleration(STEPPER_ACCELERATION);
      stepper.stop();
    }

    void stop() {
      stepper.stop();
      resetting = false;
      slowResetActive = false;
      atEnd = false;
      digitalWrite(enPin, HIGH);
    }

    bool resetTrack() {
      if (startLS.isCurrentlyPressed()) {
        stepper.stop();
        stepper.setCurrentPosition(0);
        resetting = false;
        atEnd = false;
        digitalWrite(enPin, HIGH);
        return true;
      }
      if (resetting) {
        return false;
      }
      resetting = true;
      atEnd = false;
      digitalWrite(enPin, LOW);
      stepper.setMaxSpeed(NORMAL_SPEED);
      stepper.setAcceleration(STEPPER_ACCELERATION);
      stepper.moveTo(stepper.currentPosition() - 1000000L);
      return false;
    }

    void findBounds() {
      if (resetting || findingBounds || slowResetActive || stepper.isRunning()) return;
      findingBounds = true;
      atEnd = false;
      if (startLS.isCurrentlyPressed()) {
        stepper.setCurrentPosition(0);
        findBoundsState = 2;
        stepper.setMaxSpeed(BOUNDS_SPEED);
        stepper.moveTo(1000000);
        digitalWrite(enPin, LOW);
      } else {
        findBoundsState = 1;
        stepper.setMaxSpeed(BOUNDS_SPEED);
        stepper.moveTo(-1000000);
        digitalWrite(enPin, LOW);
      }
    }

    TrackError moveNextPosition() {
      if (tracklength == 0) return BUSY;
      if (resetting || findingBounds) return BUSY;
      if (isTrackAtEnd()) return LIMIT_HIT;
      if (slowResetActive) slowResetActive = false;
      if (stepSize == 0) stepSize = tracklength / subdivs;
      long target = stepper.currentPosition() + stepSize;
      if (target >= tracklength) target = tracklength;
      stepper.setMaxSpeed(NORMAL_SPEED);
      stepper.moveTo(target);
      digitalWrite(enPin, LOW);
      return NO_ERROR;
    }

    void slowReset() {
      if (resetting || findingBounds) return;
      if (!slowResetActive && !stepper.isRunning()) {
        slowResetActive = true;
        digitalWrite(enPin, LOW);
        stepper.setMaxSpeed(SLOW_SPEED);
        stepper.setAcceleration(300);
        stepper.moveTo(-1000000);
      }
    }

    void run() {
      stepper.run();
      handleLimitSwitches();
      updateDriverState();
    }

    bool isTrackAtEnd() {
      return (stepper.currentPosition() >= tracklength && tracklength > 0) || endLS.isCurrentlyPressed();
    }

    bool isFindingBounds()    { return findingBounds; }
    bool isResetting()        { return resetting; }
    bool isAtStart()          { return startLS.isCurrentlyPressed(); }
    bool isAtEnd()            { return endLS.isCurrentlyPressed(); }
    bool isMoving()           { return stepper.isRunning(); }
    long getCurrentPosition() { return stepper.currentPosition(); }
    long getTrackLength()     { return tracklength; }

    void driftBack(int steps, int speed) {
      if (resetting || findingBounds) return;
      if (startLS.isCurrentlyPressed()) return;
      if (slowResetActive) slowResetActive = false;
      long currentPos = stepper.currentPosition();
      long targetPos = currentPos - steps;
      if (targetPos < 0) targetPos = 0;
      if (targetPos != currentPos) {
        stepper.setMaxSpeed(speed);
        stepper.moveTo(targetPos);
        digitalWrite(enPin, LOW);
      }
    }

    void moveToPosition(long targetPos, int speed) {
      if (resetting || findingBounds) return;
      if (targetPos < 0) targetPos = 0;
      if (tracklength > 0 && targetPos > tracklength) targetPos = tracklength;
      if (slowResetActive) slowResetActive = false;
      stepper.setMaxSpeed(speed);
      stepper.moveTo(targetPos);
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
    bool atEnd;

    void handleLimitSwitches() {
      if (resetting) {
        if (startLS.isCurrentlyPressed()) {
          stepper.stop();
          stepper.setCurrentPosition(0);
          resetting = false;
          atEnd = false;
          digitalWrite(enPin, HIGH);
        }
      }
      else if (findingBounds) {
        if (findBoundsState == 1) {
          if (startLS.isCurrentlyPressed()) {
            stepper.stop();
            stepper.setCurrentPosition(0);
            findBoundsState = 2;
            stepper.setMaxSpeed(BOUNDS_SPEED);
            stepper.moveTo(1000000);
            digitalWrite(enPin, LOW);
          }
        } else if (findBoundsState == 2) {
          if (endLS.isCurrentlyPressed()) {
            stepper.stop();
            tracklength = stepper.currentPosition();
            stepSize = tracklength / subdivs;
            findBoundsState = 0;
            findingBounds = false;
            atEnd = true;
            digitalWrite(enPin, HIGH);
            Serial.print("Track bounds found: ");
            Serial.print(tracklength);
            Serial.println(" steps");
          }
        }
      }
      else if (slowResetActive) {
        if (startLS.isCurrentlyPressed()) {
          stepper.stop();
          stepper.setCurrentPosition(0);
          slowResetActive = false;
          atEnd = false;
          digitalWrite(enPin, HIGH);
        }
      }
      else {
        if (endLS.isCurrentlyPressed() && !atEnd) {
          stepper.stop();
          atEnd = true;
          digitalWrite(enPin, HIGH);
        }
        if (startLS.isCurrentlyPressed()) {
          stepper.stop();
          stepper.setCurrentPosition(0);
          atEnd = false;
          digitalWrite(enPin, HIGH);
        }
      }
    }

    void updateDriverState() {
      if (!resetting && !findingBounds && !slowResetActive) {
        if (!stepper.isRunning() && digitalRead(enPin) == LOW) {
          digitalWrite(enPin, HIGH);
        }
      }
    }
};

//======================================================================================
// MODE STATE STRUCTS
//======================================================================================
struct Mode1State {
  bool gameStarted = false;
  bool gameWon = false;
  bool waitingForReset = false;
  bool isResetting = false;
  bool isMoving = false;
  void reset() { gameStarted = false; gameWon = false; waitingForReset = false; isResetting = false; isMoving = false; }
};

struct Mode2State {
  bool gameStarted = false;
  bool gameWon = false;
  int winner = 0;
  bool waitingForReset = false;
  bool isResetting = false;
  bool track1Moving = false;
  bool track2Moving = false;
  void reset() { gameStarted = false; gameWon = false; winner = 0; waitingForReset = false; isResetting = false; track1Moving = false; track2Moving = false; }
};

struct Mode3State {
  bool gameStarted = false;
  bool gameWon = false;
  bool waitingForReset = false;
  bool isResetting = false;
  bool isMovingForward = false;
  bool isDrifting = false;
  unsigned long lastDriftTime = 0;
  unsigned long lastStepTime = 0;
  void reset() { gameStarted = false; gameWon = false; waitingForReset = false; isResetting = false; isMovingForward = false; isDrifting = false; lastDriftTime = 0; lastStepTime = 0; }
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
MyLightstrip lightStrip;
MyPressureplate pressurePlate1(PRES1);
MyPressureplate pressurePlate2(PRES2);
MyTrack track1(ENBL_PIN1, STEP_PIN1, DIR_PIN1, &Serial1, TRK1_LS1, TRK1_LS2);
MyTrack track2(ENBL_PIN2, STEP_PIN2, DIR_PIN2, &Serial2, TRK2_LS1, TRK2_LS2);

//======================================================================================
// HELPER FUNCTIONS
//======================================================================================
void updateModeLEDs() {
  switch (currentMode) {
    case MODE_STANDBY:     led1.turnOff(); led2.turnOff(); break;
    case MODE_SINGLE_EASY: led1.turnOn();  led2.turnOff(); break;
    case MODE_TWO_PLAYER:  led1.turnOff(); led2.turnOn();  break;
    case MODE_SINGLE_HARD: led1.turnOn();  led2.turnOn();  break;
  }
}

void setGameOn(bool on) {
  if (on == gameOn) return;
  gameOn = on;
  if (on) {
    statusLED.turnOn();
    updateModeLEDs();
  } else {
    track1.stop();
    track2.stop();
    statusLED.turnOff();
    led1.turnOff();
    led2.turnOff();
    lightStrip.turnOff();
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
      case 'l':
        Serial.print("TRK1 Start (pin 25): ");
        Serial.println(digitalRead(TRK1_LS1) == LOW ? "PRESSED" : "OPEN");
        Serial.print("TRK1 End   (pin 23): ");
        Serial.println(digitalRead(TRK1_LS2) == LOW ? "PRESSED" : "OPEN");
        Serial.print("TRK2 Start (pin 29): ");
        Serial.println(digitalRead(TRK2_LS1) == LOW ? "PRESSED" : "OPEN");
        Serial.print("TRK2 End   (pin 27): ");
        Serial.println(digitalRead(TRK2_LS2) == LOW ? "PRESSED" : "OPEN");
        break;
      case 'm':
        Serial.print("Current Mode: ");
        Serial.println(currentMode);
        break;
      case 'h':
        Serial.println("Commands: 1=Track1Pos 2=Track2Pos p=Pressure b=Button l=LimitSwitches m=Mode h=Help");
        break;
    }
  }
}

//======================================================================================
// MODE FUNCTIONS
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
    Serial.println("Mode 1: Single Player Easy");
  }

  if (!state.gameStarted) {
    if (!track1.isAtStart()) {
      track1.resetTrack();
      lightStrip.changePattern(RESETTING);
      return;
    }
    state.gameStarted = true;
    lightStrip.changePattern(IDLE);
    Serial.println("Track at start - ready!");
  }

  if (state.gameWon) {
    lightStrip.changePattern(PLAYER1_WIN);
    if (startButton.holdTimeOnce(0)) state.waitingForReset = true;
    if (state.waitingForReset && !state.isResetting) {
      lightStrip.changePattern(RESETTING);
      state.isResetting = true;
    }
    if (state.isResetting) {
      track1.resetTrack();
      if (track1.isAtStart()) {
        state.reset();
        lightStrip.changePattern(IDLE);
        Serial.println("Ready to play again");
      }
    }
    return;
  }

  if (state.isMoving) {
    lightStrip.changePattern(MOVING);
    pressurePlate1.update();
    if (!track1.isMoving()) {
      state.isMoving = false;
      if (track1.isTrackAtEnd()) {
        state.gameWon = true;
        lightStrip.changePattern(PLAYER1_WIN);
        Serial.println("PLAYER WINS!");
      } else {
        lightStrip.changePattern(IDLE);
      }
    }
  } else {
    lightStrip.changePattern(IDLE);
    pressurePlate1.update();
    if (pressurePlate1.stepTriggered()) {
      if (!track1.isTrackAtEnd()) {
        TrackError err = track1.moveNextPosition();
        if (err == NO_ERROR) {
          state.isMoving = true;
          Serial.println("Step detected");
        }
      }
    }
  }
}

void runMode2() {
  static Mode2State state;
  if (modeJustChanged) {
    state.reset();
    lightStrip.changePattern(IDLE);
    Serial.println("Mode 2: Two Player Competitive");
  }

  if (!state.gameStarted) {
    bool t1Ready = track1.isAtStart();
    bool t2Ready = track2.isAtStart();
    if (!t1Ready || !t2Ready) {
      if (!t1Ready) track1.resetTrack();
      if (!t2Ready) track2.resetTrack();
      lightStrip.changePattern(RESETTING);
      return;
    }
    state.gameStarted = true;
    lightStrip.changePattern(IDLE);
    Serial.println("Both tracks at start - ready!");
  }

  if (state.gameWon) {
    if (state.winner == 1) lightStrip.changePattern(PLAYER1_WIN);
    else if (state.winner == 2) lightStrip.changePattern(PLAYER2_WIN);
    if (startButton.holdTimeOnce(0)) state.waitingForReset = true;
    if (state.waitingForReset && !state.isResetting) {
      lightStrip.changePattern(RESETTING);
      state.isResetting = true;
    }
    if (state.isResetting) {
      track1.resetTrack();
      track2.resetTrack();
      if (track1.isAtStart() && track2.isAtStart()) {
        state.reset();
        lightStrip.changePattern(IDLE);
        Serial.println("Ready to play again");
      }
    }
    return;
  }

  pressurePlate1.update();
  pressurePlate2.update();

  // Player 1
  if (state.track1Moving) {
    if (!track1.isMoving()) {
      state.track1Moving = false;
      if (track1.isTrackAtEnd()) {
        state.gameWon = true;
        state.winner = 1;
        lightStrip.changePattern(PLAYER1_WIN);
        Serial.println("PLAYER 1 WINS!");
        return;
      }
    }
  } else if (!state.gameWon) {
    if (pressurePlate1.stepTriggered()) {
      if (!track1.isTrackAtEnd()) {
        TrackError err = track1.moveNextPosition();
        if (err == NO_ERROR) {
          state.track1Moving = true;
          Serial.println("Player 1 step");
        }
      }
    }
  }

  // Player 2
  if (state.track2Moving) {
    if (!track2.isMoving()) {
      state.track2Moving = false;
      if (track2.isTrackAtEnd()) {
        state.gameWon = true;
        state.winner = 2;
        lightStrip.changePattern(PLAYER2_WIN);
        Serial.println("PLAYER 2 WINS!");
        return;
      }
    }
  } else if (!state.gameWon) {
    if (pressurePlate2.stepTriggered()) {
      if (!track2.isTrackAtEnd()) {
        TrackError err = track2.moveNextPosition();
        if (err == NO_ERROR) {
          state.track2Moving = true;
          Serial.println("Player 2 step");
        }
      }
    }
  }

  if (state.track1Moving || state.track2Moving) lightStrip.changePattern(MOVING);
  else lightStrip.changePattern(IDLE);
}

void runMode3() {
  static Mode3State state;
  if (modeJustChanged) {
    state.reset();
    lightStrip.changePattern(IDLE);
    Serial.println("Mode 3: Hard Mode - Track drifts back!");
  }

  if (!state.gameStarted) {
    if (!track1.isAtStart()) {
      track1.resetTrack();
      lightStrip.changePattern(RESETTING);
      return;
    }
    state.gameStarted = true;
    lightStrip.changePattern(IDLE);
    Serial.println("Track at start - ready!");
  }

  if (state.gameWon) {
    lightStrip.changePattern(PLAYER1_WIN);
    if (startButton.holdTimeOnce(0)) state.waitingForReset = true;
    if (state.waitingForReset && !state.isResetting) {
      lightStrip.changePattern(RESETTING);
      state.isResetting = true;
    }
    if (state.isResetting) {
      track1.resetTrack();
      if (track1.isAtStart()) {
        state.reset();
        lightStrip.changePattern(IDLE);
        Serial.println("Ready to play again");
      }
    }
    return;
  }

  if (state.isMovingForward) {
    lightStrip.changePattern(MOVING);
    pressurePlate1.update();
    if (!track1.isMoving()) {
      state.isMovingForward = false;
      if (track1.isTrackAtEnd()) {
        state.gameWon = true;
        lightStrip.changePattern(PLAYER1_WIN);
        Serial.println("Reached the end!");
        return;
      }
      if (!track1.isAtStart()) {
        state.isDrifting = true;
        state.lastDriftTime = millis();
        Serial.println("Starting drift");
      }
    }
  } else {
    lightStrip.changePattern(IDLE);
    pressurePlate1.update();
    if (pressurePlate1.stepTriggered() && !state.gameWon) {
      if (!track1.isTrackAtEnd()) {
        TrackError err = track1.moveNextPosition();
        if (err == NO_ERROR) {
          state.isMovingForward = true;
          state.isDrifting = false;
          state.lastStepTime = millis();
          Serial.println("Forward move");
        }
      }
    }
  }

  if (state.isDrifting && !state.gameWon && !state.isMovingForward) {
    if (track1.isAtStart()) {
      state.isDrifting = false;
      Serial.println("At start - drift stopped");
    } else if (millis() - state.lastStepTime >= DRIFT_GRACE_PERIOD_MS) {
      if (millis() - state.lastDriftTime >= DRIFT_INTERVAL_MS) {
        state.lastDriftTime = millis();
        if (!track1.isMoving()) track1.driftBack(DRIFT_STEPS_PER_INTERVAL, DRIFT_SPEED);
      }
    }
  }
}

//======================================================================================
// SETUP
//======================================================================================
void setup() {
  Serial.begin(9600);
  delay(1000);
  Serial.println("START");

  startButton.begin();
  statusLED.begin();
  led1.begin();
  led2.begin();
  pressurePlate1.begin();
  pressurePlate2.begin();

  lightStrip.begin();
  Serial.println("FastLED ok");

  track1.begin();
  Serial.println("Track1 ok");

  track2.begin();
  Serial.println("Track2 ok");

  wdt_enable(WDTO_2S);
  Serial.println("WDT ok");

  setGameOn(false);

  Serial.println("=== CALIBRATION MODE ===");
  Serial.println("Press button to find track bounds");

  for (int i = 0; i < 2; i++) {
    statusLED.turnOn();  delay(300);
    statusLED.turnOff(); delay(300);
    wdt_reset();
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

  unsigned long boundsStart = millis();
  while (track1.isFindingBounds() || track2.isFindingBounds()) {
    wdt_reset();
    track1.run();
    track2.run();
    if (millis() - boundsStart >= CALIBRATION_TIMEOUT_MS) {
      Serial.println("ERROR: findBounds timed out! Check limit switches.");
      Serial.print("Track1 findingBounds: "); Serial.println(track1.isFindingBounds());
      Serial.print("Track2 findingBounds: "); Serial.println(track2.isFindingBounds());
      while (true) { wdt_reset(); }
    }
  }

  delay(200);
  wdt_reset();

  Serial.println("Returning to start...");
  unsigned long resetStart = millis();
  while (!track1.isAtStart() || !track2.isAtStart()) {
    wdt_reset();
    track1.resetTrack();
    track2.resetTrack();
    track1.run();
    track2.run();
    if (millis() - resetStart >= CALIBRATION_TIMEOUT_MS) {
      Serial.println("ERROR: Reset to start timed out! Check start limit switches.");
      Serial.print("Track1 at start: "); Serial.println(track1.isAtStart());
      Serial.print("Track2 at start: "); Serial.println(track2.isAtStart());
      while (true) { wdt_reset(); }
    }
  }

  statusLED.turnOn();
  Serial.println("=== CALIBRATION COMPLETE ===");
  Serial.println("System ready. Press button to start game.");
  Serial.println("Type h for debug commands.");
}

//======================================================================================
// MAIN LOOP
//======================================================================================
void loop() {
  wdt_reset();

  processSerialCommands();

  if (!gameOn) {
    lightStrip.play();
    lightStrip.changePattern(STANDBY);
    if (startButton.holdTimeOnce(0)) {
      setGameOn(true);
      currentMode = MODE_STANDBY;
      modeJustChanged = true;
      Serial.println("Game ON");
    }
    return;
  }

  track1.run();
  track2.run();
  lightStrip.play();

  // Check 2-second hold first — if it fires, skip all shorter hold checks
  if (startButton.holdTimeOnce(2)) {
    setGameOn(false);
    Serial.println("Game OFF");
    return;
  }

  if (startButton.holdTimeOnce(1)) {
    bool blockedByMovement = (currentMode != MODE_SINGLE_HARD) &&
                             (track1.isMoving() || track2.isMoving());
    if (!blockedByMovement) {
      track1.stop();
      track2.stop();
      currentMode = (GameMode)((currentMode + 1) % 4);
      modeJustChanged = true;
      Serial.print("Mode changed to: ");
      Serial.println(currentMode);
      updateModeLEDs();
    } else {
      Serial.println("Mode change ignored - tracks still moving");
    }
  }

  switch (currentMode) {
    case MODE_STANDBY:     runMode0(); break;
    case MODE_SINGLE_EASY: runMode1(); break;
    case MODE_TWO_PLAYER:  runMode2(); break;
    case MODE_SINGLE_HARD: runMode3(); break;
  }

  modeJustChanged = false;
}
