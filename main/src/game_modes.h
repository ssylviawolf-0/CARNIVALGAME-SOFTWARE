#pragma once
#include "hardware.h"
#include "track.h"

// Globals defined in main.ino
extern MyButton        startButton;
extern MyLED           statusLED;
extern MyLED           led1;
extern MyLED           led2;
extern MyLightstrip    lightStrip;
extern MyPressureplate pressurePlate1;
extern MyPressureplate pressurePlate2;
extern MyTrack         track1;
extern MyTrack         track2;
extern bool            gameOn;
extern GameMode        currentMode;
extern bool            modeJustChanged;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
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
  if (!Serial.available()) return;
  switch (Serial.read()) {
    case '1':
      Serial.print("T1 pos: "); Serial.println(track1.getCurrentPosition()); break;
    case '2':
      Serial.print("T2 pos: "); Serial.println(track2.getCurrentPosition()); break;
    case 'p':
      Serial.print("P1="); Serial.print(pressurePlate1.rawVal());
      Serial.print(" P2="); Serial.println(pressurePlate2.rawVal()); break;
    case 'b':
      Serial.println(digitalRead(STRT_PIN) == LOW ? "BTN: PRESSED" : "BTN: open"); break;
    case 'l':
      Serial.print("T1S(25)="); Serial.println(digitalRead(TRK1_LS1) == LOW ? "HIT" : "open");
      Serial.print("T1E(23)="); Serial.println(digitalRead(TRK1_LS2) == LOW ? "HIT" : "open");
      Serial.print("T2S(29)="); Serial.println(digitalRead(TRK2_LS1) == LOW ? "HIT" : "open");
      Serial.print("T2E(27)="); Serial.println(digitalRead(TRK2_LS2) == LOW ? "HIT" : "open"); break;
    case 'm':
      Serial.print("Mode: "); Serial.println(currentMode); break;
    case 'h':
      Serial.println("1=T1 2=T2 p=pressure b=button l=limits m=mode h=help"); break;
  }
}

// ---------------------------------------------------------------------------
// Mode 0 — Standby
// ---------------------------------------------------------------------------
void runMode0() {
  if (modeJustChanged) lightStrip.changePattern(STANDBY);
}

// ---------------------------------------------------------------------------
// Mode 1 — Single Player Easy
// Pressure plate release advances track one subdivision.
// ---------------------------------------------------------------------------
void runMode1() {
  static Mode1State state;
  if (modeJustChanged) {
    state.reset();
    lightStrip.changePattern(IDLE);
    Serial.println("Mode 1: Single Easy");
  }

  if (!state.gameStarted) {
    if (!track1.isAtStart()) { track1.resetTrack(); lightStrip.changePattern(RESETTING); return; }
    state.gameStarted = true;
    lightStrip.changePattern(IDLE);
    Serial.println("Ready");
  }

  if (state.gameWon) {
    lightStrip.changePattern(PLAYER1_WIN);
    if (startButton.holdTimeOnce(0)) state.waitingForReset = true;
    if (state.waitingForReset && !state.isResetting) { lightStrip.changePattern(RESETTING); state.isResetting = true; }
    if (state.isResetting) {
      track1.resetTrack();
      if (track1.isAtStart()) { state.reset(); lightStrip.changePattern(IDLE); Serial.println("Ready"); }
    }
    return;
  }

  pressurePlate1.update();
  if (state.isMoving) {
    lightStrip.changePattern(MOVING);
    if (!track1.isMoving()) {
      state.isMoving = false;
      if (track1.isTrackAtEnd()) { state.gameWon = true; lightStrip.changePattern(PLAYER1_WIN); Serial.println("P1 WINS!"); }
      else lightStrip.changePattern(IDLE);
    }
  } else {
    lightStrip.changePattern(IDLE);
    if (pressurePlate1.stepTriggered() && !track1.isTrackAtEnd()) {
      if (track1.moveNextPosition() == NO_ERROR) { state.isMoving = true; Serial.println("Step"); }
    }
  }
}

// ---------------------------------------------------------------------------
// Mode 2 — Two Player Competitive
// ---------------------------------------------------------------------------
void runMode2() {
  static Mode2State state;
  if (modeJustChanged) {
    state.reset();
    lightStrip.changePattern(IDLE);
    Serial.println("Mode 2: Two Player");
  }

  if (!state.gameStarted) {
    bool t1ok = track1.isAtStart(), t2ok = track2.isAtStart();
    if (!t1ok) track1.resetTrack();
    if (!t2ok) track2.resetTrack();
    if (!t1ok || !t2ok) { lightStrip.changePattern(RESETTING); return; }
    state.gameStarted = true;
    lightStrip.changePattern(IDLE);
    Serial.println("Ready");
  }

  if (state.gameWon) {
    lightStrip.changePattern(state.winner == 1 ? PLAYER1_WIN : PLAYER2_WIN);
    if (startButton.holdTimeOnce(0)) state.waitingForReset = true;
    if (state.waitingForReset && !state.isResetting) { lightStrip.changePattern(RESETTING); state.isResetting = true; }
    if (state.isResetting) {
      track1.resetTrack(); track2.resetTrack();
      if (track1.isAtStart() && track2.isAtStart()) { state.reset(); lightStrip.changePattern(IDLE); Serial.println("Ready"); }
    }
    return;
  }

  pressurePlate1.update();
  pressurePlate2.update();

  if (state.track1Moving) {
    if (!track1.isMoving()) {
      state.track1Moving = false;
      if (track1.isTrackAtEnd()) { state.gameWon = true; state.winner = 1; lightStrip.changePattern(PLAYER1_WIN); Serial.println("P1 WINS!"); return; }
    }
  } else if (!state.gameWon && pressurePlate1.stepTriggered() && !track1.isTrackAtEnd()) {
    if (track1.moveNextPosition() == NO_ERROR) { state.track1Moving = true; Serial.println("P1 step"); }
  }

  if (state.track2Moving) {
    if (!track2.isMoving()) {
      state.track2Moving = false;
      if (track2.isTrackAtEnd()) { state.gameWon = true; state.winner = 2; lightStrip.changePattern(PLAYER2_WIN); Serial.println("P2 WINS!"); return; }
    }
  } else if (!state.gameWon && pressurePlate2.stepTriggered() && !track2.isTrackAtEnd()) {
    if (track2.moveNextPosition() == NO_ERROR) { state.track2Moving = true; Serial.println("P2 step"); }
  }

  lightStrip.changePattern(state.track1Moving || state.track2Moving ? MOVING : IDLE);
}

// ---------------------------------------------------------------------------
// Mode 3 — Single Player Hard  (track drifts back between presses)
// ---------------------------------------------------------------------------
void runMode3() {
  static Mode3State state;
  if (modeJustChanged) {
    state.reset();
    lightStrip.changePattern(IDLE);
    Serial.println("Mode 3: Hard");
  }

  if (!state.gameStarted) {
    if (!track1.isAtStart()) { track1.resetTrack(); lightStrip.changePattern(RESETTING); return; }
    state.gameStarted = true;
    lightStrip.changePattern(IDLE);
    Serial.println("Ready");
  }

  if (state.gameWon) {
    lightStrip.changePattern(PLAYER1_WIN);
    if (startButton.holdTimeOnce(0)) state.waitingForReset = true;
    if (state.waitingForReset && !state.isResetting) { lightStrip.changePattern(RESETTING); state.isResetting = true; }
    if (state.isResetting) {
      track1.resetTrack();
      if (track1.isAtStart()) { state.reset(); lightStrip.changePattern(IDLE); Serial.println("Ready"); }
    }
    return;
  }

  pressurePlate1.update();
  if (state.isMovingForward) {
    lightStrip.changePattern(MOVING);
    if (!track1.isMoving()) {
      state.isMovingForward = false;
      if (track1.isTrackAtEnd()) { state.gameWon = true; lightStrip.changePattern(PLAYER1_WIN); Serial.println("P1 WINS!"); return; }
      if (!track1.isAtStart()) { state.isDrifting = true; state.lastDriftTime = millis(); Serial.println("Drift"); }
    }
  } else {
    lightStrip.changePattern(IDLE);
    if (pressurePlate1.stepTriggered() && !track1.isTrackAtEnd()) {
      if (track1.moveNextPosition() == NO_ERROR) {
        state.isMovingForward = true;
        state.isDrifting      = false;
        state.lastStepTime    = millis();
        Serial.println("Forward");
      }
    }
  }

  if (state.isDrifting && !state.gameWon && !state.isMovingForward) {
    if (track1.isAtStart()) {
      state.isDrifting = false;
    } else if (millis() - state.lastStepTime >= DRIFT_GRACE_PERIOD_MS) {
      if (millis() - state.lastDriftTime >= DRIFT_INTERVAL_MS && !track1.isMoving()) {
        state.lastDriftTime = millis();
        track1.driftBack(DRIFT_STEPS_PER_INTERVAL, DRIFT_SPEED);
      }
    }
  }
}
