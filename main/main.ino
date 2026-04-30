#include <avr/wdt.h>
#include <TMCStepper.h>
#include <AccelStepper.h>
#include <FastLED.h>

#include "src/config.h"
#include "src/hardware.h"
#include "src/track.h"
#include "src/game_modes.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
CRGB leds[NUM_LEDS];  // shared with MyLightstrip via extern in hardware.h

bool     gameOn        = false;
GameMode currentMode   = MODE_STANDBY;
bool     modeJustChanged = false;

// ---------------------------------------------------------------------------
// Object Instances
// ---------------------------------------------------------------------------
MyButton        startButton(STRT_PIN);
MyLED           statusLED(START_LED);
MyLED           led1(LED1);
MyLED           led2(LED2);
MyLightstrip    lightStrip;
MyPressureplate pressurePlate1(PRES1);
MyPressureplate pressurePlate2(PRES2);
MyTrack track1(ENBL_PIN1, STEP_PIN1, DIR_PIN1, &Serial1, TRK1_LS1, TRK1_LS2);
MyTrack track2(ENBL_PIN2, STEP_PIN2, DIR_PIN2, &Serial2, TRK2_LS1, TRK2_LS2);

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(9600);
  delay(1000);
  Serial.println("START");

  startButton.begin();
  statusLED.begin(); led1.begin(); led2.begin();
  pressurePlate1.begin(); pressurePlate2.begin();
  lightStrip.begin();
  track1.begin();
  track2.begin();

  wdt_enable(WDTO_2S);
  setGameOn(false);

  // Two blinks signal ready for calibration
  for (int i = 0; i < 2; i++) {
    statusLED.turnOn();  delay(300); wdt_reset();
    statusLED.turnOff(); delay(300); wdt_reset();
  }

  Serial.println("=== CALIBRATION: press button to begin ===");
  while (!startButton.isPressed()) { wdt_reset(); delay(50); }
  delay(200);
  while (startButton.isPressed())  { wdt_reset(); delay(10); }

  Serial.println("Finding bounds...");
  track1.findBounds();
  track2.findBounds();

  unsigned long t = millis();
  while (track1.isFindingBounds() || track2.isFindingBounds()) {
    wdt_reset(); track1.run(); track2.run();
    if (millis() - t >= CALIBRATION_TIMEOUT_MS) {
      Serial.println("ERROR: findBounds timeout — check limit switches");
      while (true) wdt_reset();
    }
  }

  delay(200); wdt_reset();

  Serial.println("Returning to start...");
  t = millis();
  while (!track1.isAtStart() || !track2.isAtStart()) {
    wdt_reset();
    track1.resetTrack(); track2.resetTrack();
    track1.run();        track2.run();
    if (millis() - t >= CALIBRATION_TIMEOUT_MS) {
      Serial.println("ERROR: reset timeout — check start limit switches");
      while (true) wdt_reset();
    }
  }

  statusLED.turnOn();
  Serial.println("=== READY. Press button to start. (h = help) ===");
}

// ---------------------------------------------------------------------------
// Main Loop
// ---------------------------------------------------------------------------
void loop() {
  wdt_reset();
  processSerialCommands();

  if (!gameOn) {
    lightStrip.play();
    lightStrip.changePattern(STANDBY);
    if (startButton.holdTimeOnce(0)) {
      setGameOn(true);
      currentMode      = MODE_STANDBY;
      modeJustChanged  = true;
      Serial.println("Game ON");
    }
    return;
  }

  track1.run(); track2.run();
  lightStrip.play();

  // 2-second hold → game off (checked first so it takes priority)
  if (startButton.holdTimeOnce(2)) {
    setGameOn(false);
    Serial.println("Game OFF");
    return;
  }

  // 1-second hold → cycle mode (blocked while tracks are moving)
  if (startButton.holdTimeOnce(1)) {
    bool moving = (currentMode != MODE_SINGLE_HARD) && (track1.isMoving() || track2.isMoving());
    if (!moving) {
      track1.stop(); track2.stop();
      currentMode     = (GameMode)((currentMode + 1) % 4);
      modeJustChanged = true;
      updateModeLEDs();
      Serial.print("Mode: "); Serial.println(currentMode);
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
