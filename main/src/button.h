#pragma once
#include "config.h"

// INPUT_PULLUP button — wire to GND.
// holdTimeOnce(0) fires once on release (tap).
// holdTimeOnce(n) fires once when held n seconds.
class MyButton {
public:
  MyButton(int pinNum) : pin(pinNum), lastState(false), pressStartTime(0), holdTriggered(false) {}

  void begin() {
    pinMode(pin, INPUT_PULLUP);
    delay(50);
    lastState = isPressed();
  }

  bool isPressed() { return digitalRead(pin) == LOW; }

  bool holdTimeOnce(int num) {
    unsigned long threshold = (unsigned long)num * 1000UL;
    bool cur = isPressed();

    if (cur && !lastState) {
      pressStartTime = millis();
      holdTriggered  = false;
    } else if (!cur && lastState) {
      if (num == 0 && !holdTriggered && pressStartTime != 0) {
        pressStartTime = 0;
        lastState = cur;
        return true;
      }
      pressStartTime = 0;
      holdTriggered  = false;
    }
    lastState = cur;

    if (num > 0 && cur && !holdTriggered && pressStartTime != 0) {
      if (millis() - pressStartTime >= threshold) {
        holdTriggered = true;
        return true;
      }
    }
    return false;
  }

private:
  int           pin;
  bool          lastState;
  unsigned long pressStartTime;
  bool          holdTriggered;
};
