#pragma once
#include "config.h"

// Debounced analog pressure sensor.
// Call update() every loop(); check stepTriggered() for a confirmed press event.
class MyPressureplate {
public:
  MyPressureplate(int pinNum)
    : pin(pinNum), debounceStart(0), pressStart(0),
      rawPressedState(false), debouncedState(false),
      stepReady(false), stepConsumed(false) {}

  void begin() { pinMode(pin, INPUT); }
  int  rawVal() { return analogRead(pin); }

  void update() {
    bool raw = (analogRead(pin) < PRESSURE_THRESHOLD);
    if (raw != rawPressedState) {
      rawPressedState = raw;
      debounceStart   = millis();
    }
    if (millis() - debounceStart >= PLATE_DEBOUNCE_MS) {
      bool prev      = debouncedState;
      debouncedState = rawPressedState;
      if (debouncedState && !prev) {
        pressStart   = millis();
        stepConsumed = stepReady = false;
      }
      if (!debouncedState && prev) {
        pressStart   = 0;
        stepReady    = stepConsumed = false;
      }
    }
    if (debouncedState && !stepConsumed && pressStart != 0) {
      if (millis() - pressStart >= PLATE_MIN_PRESS_MS) stepReady = true;
    }
  }

  bool stepTriggered() {
    if (stepReady && !stepConsumed) {
      stepConsumed = true;
      stepReady    = false;
      return true;
    }
    return false;
  }

  bool isPressed() { return debouncedState; }

private:
  int           pin;
  unsigned long debounceStart, pressStart;
  bool          rawPressedState, debouncedState, stepReady, stepConsumed;
};
