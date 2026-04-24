#pragma once
#include <TMCStepper.h>
#include <AccelStepper.h>
#include "limit_switch.h"

// ---------------------------------------------------------------------------
// MyTrack
// Stepper motor track with TMC2209 UART driver, two limit switches,
// bounds calibration, and subdivision-based movement control.
// ---------------------------------------------------------------------------
class MyTrack {
public:
  MyTrack(int enPin, int stepPin, int dirPin, HardwareSerial* serial,
          int startPin, int endPin)
    : enPin(enPin), hwSerial(serial),
      startLS(startPin), endLS(endPin),
      driver(serial, TMC_R_SENSE, 0),
      stepper(AccelStepper::DRIVER, stepPin, dirPin),
      tracklength(0), subdivs(TRACK_SUBDIVISIONS), stepSize(0),
      resetting(false), findingBounds(false), findBoundsState(0),
      slowResetActive(false), atEnd(false) {}

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
    driver.en_spreadCycle(0);   // StealthChop
    driver.TCOOLTHRS(0);
    driver.SGTHRS(0);
    stepper.setMaxSpeed(MAX_STEPPER_SPEED);
    stepper.setAcceleration(STEPPER_ACCELERATION);
    stepper.stop();
  }

  void stop() {
    stepper.stop();
    resetting = slowResetActive = atEnd = false;
    digitalWrite(enPin, HIGH);
  }

  // Returns true once the track is confirmed at the start position.
  bool resetTrack() {
    if (startLS.isCurrentlyPressed()) {
      stepper.stop();
      stepper.setCurrentPosition(0);
      resetting = atEnd = false;
      digitalWrite(enPin, HIGH);
      return true;
    }
    if (resetting) return false;
    resetting = true;
    atEnd = false;
    digitalWrite(enPin, LOW);
    stepper.setMaxSpeed(NORMAL_SPEED);
    stepper.setAcceleration(STEPPER_ACCELERATION);
    stepper.moveTo(stepper.currentPosition() - 1000000L);
    return false;
  }

  // Drive to both limit switches to measure track length; call run() in a loop until isFindingBounds() == false.
  void findBounds() {
    if (resetting || findingBounds || slowResetActive || stepper.isRunning()) return;
    findingBounds = true;
    atEnd = false;
    if (startLS.isCurrentlyPressed()) {
      stepper.setCurrentPosition(0);
      findBoundsState = 2;
    } else {
      findBoundsState = 1;
    }
    stepper.setMaxSpeed(BOUNDS_SPEED);
    stepper.moveTo(findBoundsState == 2 ? 1000000L : -1000000L);
    digitalWrite(enPin, LOW);
  }

  TrackError moveNextPosition() {
    if (tracklength == 0 || resetting || findingBounds) return BUSY;
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
    if (resetting || findingBounds || slowResetActive || stepper.isRunning()) return;
    slowResetActive = true;
    digitalWrite(enPin, LOW);
    stepper.setMaxSpeed(SLOW_SPEED);
    stepper.setAcceleration(300);
    stepper.moveTo(-1000000L);
  }

  void driftBack(int steps, int speed) {
    if (resetting || findingBounds || startLS.isCurrentlyPressed()) return;
    if (slowResetActive) slowResetActive = false;
    long target = stepper.currentPosition() - (long)steps;
    if (target < 0) target = 0;
    if (target != stepper.currentPosition()) {
      stepper.setMaxSpeed(speed);
      stepper.moveTo(target);
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

  // Must be called every loop() iteration.
  void run() {
    stepper.run();
    handleLimitSwitches();
    updateDriverState();
  }

  bool isTrackAtEnd()       { return (tracklength > 0 && stepper.currentPosition() >= tracklength) || endLS.isCurrentlyPressed(); }
  bool isFindingBounds()    { return findingBounds; }
  bool isResetting()        { return resetting; }
  bool isAtStart()          { return startLS.isCurrentlyPressed(); }
  bool isAtEnd()            { return endLS.isCurrentlyPressed(); }
  bool isMoving()           { return stepper.isRunning(); }
  long getCurrentPosition() { return stepper.currentPosition(); }
  long getTrackLength()     { return tracklength; }

private:
  int             enPin;
  HardwareSerial* hwSerial;
  MyLimitSwitch   startLS, endLS;
  TMC2209Stepper  driver;
  AccelStepper    stepper;
  long            tracklength, stepSize;
  int             subdivs, findBoundsState;
  bool            resetting, findingBounds, slowResetActive, atEnd;

  void handleLimitSwitches() {
    if (resetting) {
      if (startLS.isCurrentlyPressed()) {
        stepper.stop();
        stepper.setCurrentPosition(0);
        resetting = atEnd = false;
        digitalWrite(enPin, HIGH);
      }
    } else if (findingBounds) {
      if (findBoundsState == 1 && startLS.isCurrentlyPressed()) {
        stepper.stop();
        stepper.setCurrentPosition(0);
        findBoundsState = 2;
        stepper.setMaxSpeed(BOUNDS_SPEED);
        stepper.moveTo(1000000L);
        digitalWrite(enPin, LOW);
      } else if (findBoundsState == 2 && endLS.isCurrentlyPressed()) {
        stepper.stop();
        tracklength     = stepper.currentPosition();
        stepSize        = tracklength / subdivs;
        findBoundsState = 0;
        findingBounds   = false;
        atEnd           = true;
        digitalWrite(enPin, HIGH);
        Serial.print("Bounds: ");
        Serial.print(tracklength);
        Serial.println(" steps");
      }
    } else if (slowResetActive) {
      if (startLS.isCurrentlyPressed()) {
        stepper.stop();
        stepper.setCurrentPosition(0);
        slowResetActive = atEnd = false;
        digitalWrite(enPin, HIGH);
      }
    } else {
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
      if (!stepper.isRunning() && digitalRead(enPin) == LOW)
        digitalWrite(enPin, HIGH);
    }
  }
};
