#pragma once
#include "config.h"

class MyLimitSwitch {
public:
  MyLimitSwitch(int pinNum) : pin(pinNum), previousState(HIGH) {}

  void reinit() {
    pinMode(pin, INPUT_PULLUP);
    delay(50);
    previousState = digitalRead(pin);
  }

  // Returns true once on the HIGH→LOW edge.
  bool isPressed() {
    bool cur   = digitalRead(pin);
    bool fired = (previousState == HIGH && cur == LOW);
    previousState = cur;
    return fired;
  }

  bool isCurrentlyPressed() { return digitalRead(pin) == LOW; }

private:
  int  pin;
  bool previousState;
};
