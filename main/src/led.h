#pragma once
#include "config.h"

class MyLED {
public:
  MyLED(int pinNum) : pin(pinNum), state(false) {}

  void begin()   { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }
  void turnOn()  { digitalWrite(pin, HIGH); state = true;  }
  void turnOff() { digitalWrite(pin, LOW);  state = false; }
  void toggle()  { if (state) turnOff(); else turnOn(); }

private:
  int  pin;
  bool state;
};
