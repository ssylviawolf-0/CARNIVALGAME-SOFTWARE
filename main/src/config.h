#pragma once

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------
enum LightPattern { STANDBY = 0, IDLE, MOVING, PLAYER1_WIN, PLAYER2_WIN, RESETTING };
enum GameMode     { MODE_STANDBY = 0, MODE_SINGLE_EASY, MODE_TWO_PLAYER, MODE_SINGLE_HARD };
enum TrackError   { NO_ERROR = 0, LIMIT_HIT = 2, BUSY = 3 };

// ---------------------------------------------------------------------------
// Pin Definitions  (Arduino Mega 2560)
// ---------------------------------------------------------------------------
#define STEP_PIN1   3
#define DIR_PIN1    2
#define ENBL_PIN1   7

#define STEP_PIN2   9
#define DIR_PIN2    8
#define ENBL_PIN2   13

#define PRES1       A0
#define PRES2       A1

#define TRK1_LS1    25   // Track 1 start limit switch
#define TRK1_LS2    23   // Track 1 end   limit switch
#define TRK2_LS1    29   // Track 2 start limit switch
#define TRK2_LS2    27   // Track 2 end   limit switch

#define STRT_PIN    48
#define START_LED   53
#define LED1        45
#define LED2        37
#define DATA_PIN    6    // WS2812B data line

// ---------------------------------------------------------------------------
// Stepper / Driver Constants
// ---------------------------------------------------------------------------
const float         TMC_R_SENSE            = 0.11f;
const int           TMC_CURRENT_MA         = 600;
const int           TMC_MICROSTEPS         = 2;
const int           MAX_STEPPER_SPEED      = 2000;
const int           STEPPER_ACCELERATION   = 1000;
const int           NORMAL_SPEED           = 1000;
const int           SLOW_SPEED             = 400;
const int           BOUNDS_SPEED           = 800;
const int           DRIFT_SPEED            = 200;

// ---------------------------------------------------------------------------
// Game / Sensor Constants
// ---------------------------------------------------------------------------
const int           PRESSURE_THRESHOLD        = 500;
const int           NUM_LEDS                  = 40;
const int           TRACK_SUBDIVISIONS        = 5;
const int           DRIFT_INTERVAL_MS         = 500;
const int           DRIFT_STEPS_PER_INTERVAL  = 100;
const unsigned long DRIFT_GRACE_PERIOD_MS     = 1000;
const unsigned long CALIBRATION_TIMEOUT_MS    = 15000;
const unsigned long PLATE_DEBOUNCE_MS         = 50;
const unsigned long PLATE_MIN_PRESS_MS        = 250;

// ---------------------------------------------------------------------------
// Mode State Structs
// ---------------------------------------------------------------------------
struct Mode1State {
  bool gameStarted, gameWon, waitingForReset, isResetting, isMoving;
  void reset() { gameStarted = gameWon = waitingForReset = isResetting = isMoving = false; }
};

struct Mode2State {
  bool gameStarted, gameWon, waitingForReset, isResetting, track1Moving, track2Moving;
  int  winner;
  void reset() {
    gameStarted = gameWon = waitingForReset = isResetting = track1Moving = track2Moving = false;
    winner = 0;
  }
};

struct Mode3State {
  bool          gameStarted, gameWon, waitingForReset, isResetting, isMovingForward, isDrifting;
  unsigned long lastDriftTime, lastStepTime;
  void reset() {
    gameStarted = gameWon = waitingForReset = isResetting = isMovingForward = isDrifting = false;
    lastDriftTime = lastStepTime = 0;
  }
};
