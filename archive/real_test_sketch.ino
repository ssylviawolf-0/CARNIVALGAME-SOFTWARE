#include <SoftwareSerial.h>
#include <TMCStepper.h>
#include <AccelStepper.h>

//CUSTOM PIN NAMES-----------------------------------------------------------------
  //Pins used forconnection with motor contoller 1
  #define STEP_PIN1 3
  #define DIR_PIN1 2
  #define ENBL_PIN1 7
  #define SW_RX1 5     // Arduino RX ← TMC2209 #1 TX (PDN_UART)
  #define SW_TX1 6     // Arduino TX → TMC2209 #1 RX (DIAG1)

  //Pins used for connection with motor controller 2
  #define STEP_PIN2 9
  #define DIR_PIN2 8
  #define ENBL_PIN2 13
  #define SW_RX2 11    // Arduino RX ← TMC2209 #2 TX (PDN_UART)
  #define SW_TX2 12    // Arduino TX → TMC2209 #2 RX (DIAG1)

  //Pins used for sensor inputs 
  #define PRES1 A0
  #define PRES2 A1

  //Pins used for limit switches on track 1
  #define TRK1_LS1 A4
  #define TRK1_LS2 A5

  //Pins used for limit switches on track 2
  #define TRK2_LS1 A6
  #define TRK2_LS2 A7

  //Pin used for start button
  #define STRT_PIN A8

  //Pins used for the status LEDs
  #define LED1 53
  #define LED2 45
  #define LED3 37

//CUSTOM CONSTANTS-----------------------------------------------------------------
  #define PRESSURE_THRESHOLD 500   // analog value below this is considered "low"
  #define WIN_POSITION 4000        // target position to win
  #define HOLD_DISABLE_TIME 750    // ms – press and hold to emergency stop

//CUSTOM VARIABLES-----------------------------------------------------------------
  int MODE = 0;
  
//CUSTOM CLASSES-------------------------------------------------------------------
  class MyButton { //custom functions and variables used to manipulate the operation of buttons in the main code
    private:
      int PIN;
      unsigned long TimePressed;
      unsigned long LastDebounceTime;
      int DebounceTime = 15;
      bool PrevVal;  // debounced state, true = pressed (active LOW)

    public:
      MyButton(int pin) {
        PIN = pin;
        pinMode(PIN, INPUT_PULLUP);   // assume pull-up, pressed = LOW
        PrevVal = false;
        TimePressed = 0;
        LastDebounceTime = 0;
      }

      bool isPressed() {
        bool reading = (digitalRead(PIN) == LOW);   // active LOW
        if (reading != PrevVal) {
          LastDebounceTime = millis();
        }
        if ((millis() - LastDebounceTime) > DebounceTime) {
          if (reading != PrevVal) {
            PrevVal = reading;
            if (PrevVal == true) {
              TimePressed = millis();   // record press start time
            }
          }
        }
        return PrevVal;
      }

      bool notPressed() {
        return !isPressed();
      }

      unsigned long timePressed() {
        if (isPressed()) {
          return millis() - TimePressed;
        } else {
          return 0;
        }
      }
  };

  class MyLED { //custom functions and variables used to manipulate the operation of LEDs in the main code
    private:
      int pin;
      bool state;

    public:
      MyLED(int p) {
        pin = p;
        pinMode(pin, OUTPUT);
        state = false;
      }

      void on() {
        digitalWrite(pin, HIGH);
        state = true;
      }

      void off() {
        digitalWrite(pin, LOW);
        state = false;
      }

      void toggle() {
        if (state) off();
        else on();
      }

      bool getState() {
        return state;
      }
  };

  class MySensor { //custom functions and variables used to manipulate the operation of LEDs in the main code 
    private:
      int PIN;
      int val;
    public:
      MySensor(int pin){
        PIN = pin;
      }

      int getVal(){
        val = analogRead(PIN);
        return val;
      }
  };

  class MyTrack { //custom functions and varibale used to manipulate the operations of the stepper motors in the main code
    private:
      SoftwareSerial swSerial;
      TMC2209Stepper driver;
      AccelStepper stepper;
      MyButton limit1;
      MyButton limit2;
      int enPin;
      bool enabled;

    public:
      // Constructor now includes both SW_RX and SW_TX pins
      MyTrack(int STEP, int DIR, int ENBL, int SW_RX, int SW_TX, int LS1, int LS2)
        : swSerial(SW_RX, SW_TX),
          driver(&swSerial, 0.11f),                // R_SENSE = 0.11 for typical TMC2209
          stepper(AccelStepper::DRIVER, STEP, DIR),
          limit1(LS1),
          limit2(LS2)
      {
        enPin = ENBL;
        pinMode(enPin, OUTPUT);
        disable();                                 // start disabled

        swSerial.begin(115200);                   // start UART
        driver.begin();
        driver.toff(4);
        driver.blank_time(24);
        driver.rms_current(500);                  // 500 mA
        driver.microsteps(2);
        driver.en_spreadCycle(0);                // enable stealthChop
        driver.pdn_disable(true);               // use PDN_UART as UART
        enabled = false;
      }

      void enable() {
        digitalWrite(enPin, LOW);    // assume active LOW enable
        enabled = true;
      }

      void disable() {
        digitalWrite(enPin, HIGH);
        enabled = false;
      }

      bool isEnabled() {
        return enabled;
      }

      void setSpeed(float speed) {
        stepper.setSpeed(speed);
      }

      void move(long steps) {
        stepper.move(steps);
      }

      void runSpeed() {
        stepper.runSpeed();
      }

      void run() {
        stepper.run();
      }

      void stop() {
        stepper.stop();      // sets speed to 0
      }

      bool atLimit1() {
        return limit1.isPressed();
      }

      bool atLimit2() {
        return limit2.isPressed();
      }

      long currentPosition() {
        return stepper.currentPosition();
      }

      void setCurrentPosition(long pos) {
        stepper.setCurrentPosition(pos);
      }
  };

//CLASS OBJECTS--------------------------------------------------------------------
  // 2 Track objects
  MyTrack track1(STEP_PIN1, DIR_PIN1, ENBL_PIN1, SW_RX1, SW_TX1, TRK1_LS1, TRK1_LS2);
  MyTrack track2(STEP_PIN2, DIR_PIN2, ENBL_PIN2, SW_RX2, SW_TX2, TRK2_LS1, TRK2_LS2);

  // 3 LED objects
  MyLED led1(LED1);
  MyLED led2(LED2);
  MyLED led3(LED3);

  // 1 Start button
  MyButton startButton(STRT_PIN);

  // 2 Pressure sensors
  MySensor pressSensor1(PRES1);
  MySensor pressSensor2(PRES2);

//CUSTOM FUNCTIONS-----------------------------------------------------------------
  // Function to cycle MODE (0→1→2→3→0) on each button press
  void handleModeChange() {
    static bool lastButtonState = false;
    bool currentButtonState = startButton.isPressed();

    if (currentButtonState == true && lastButtonState == false) {
      MODE++;
      if (MODE > 3) MODE = 0;
    }
    lastButtonState = currentButtonState;
  }

  // Set LED2/LED3 according to current MODE
  void updateModeLEDs() {
    switch (MODE) {
      case 0:
        led2.off();
        led3.off();
        break;
      case 1:
        led2.off();
        led3.on();
        break;
      case 2:
        led2.on();
        led3.off();
        break;
      case 3:
        led2.on();
        led3.on();
        break;
      default:
        led2.off();
        led3.off();
        break;
    }
  }

//SETUP_CODE-----------------------------------------------------------------------
void setup() {
  Serial.begin(9600);

  // Additional pinModes for clarity (STEP/DIR already handled by AccelStepper)
  pinMode(STEP_PIN1, OUTPUT);
  pinMode(DIR_PIN1, OUTPUT);
  pinMode(STEP_PIN2, OUTPUT);
  pinMode(DIR_PIN2, OUTPUT);
  pinMode(PRES1, INPUT);
  pinMode(PRES2, INPUT);

  led1.on();                     // LED1 always on
  updateModeLEDs();             // initial LED states
}

void loop() {
  // --- Emergency stop: press and hold for HOLD_DISABLE_TIME ---
  static bool emergencyStopTriggered = false;
  if (!emergencyStopTriggered && startButton.timePressed() >= HOLD_DISABLE_TIME) {
    emergencyStopTriggered = true;
    
    // Stop and disable both motors
    track1.stop();
    track2.stop();
    track1.disable();
    track2.disable();
    
    // Cancel any ongoing homing
    // (static variables declared later in loop; we set them now via direct assignment)
    // Since they are not yet declared, we will set them after their declaration.
    // We'll use a flag to indicate that emergency stop should be processed later.
    // Simpler: we can just set MODE=0 and rely on the rest of the loop to clear state.
    MODE = 0;
    updateModeLEDs();
    
    Serial.println("EMERGENCY STOP: motors disabled, mode 0");
  }
  if (emergencyStopTriggered && startButton.notPressed()) {
    emergencyStopTriggered = false;
  }

  // --- Homing state machine variables (persist between loops) ---
  static bool homingActive = false;       // true while homing in progress
  static bool home1Done = false;
  static bool home2Done = false;
  static bool homingDoneForMode1 = false; // true if we've already homed in current Mode1 session

  // If emergency stop was just triggered, we also need to reset homing flags.
  // Since emergencyStopTriggered is set above, we can do it here.
  if (emergencyStopTriggered) {
    homingActive = false;
    home1Done = false;
    home2Done = false;
    homingDoneForMode1 = false;
    // No need to repeat motor disable, already done
  }

  // -----------------------------------------------------------------
  // 1. If homing is active → only run homing, skip everything else
  // -----------------------------------------------------------------
  if (homingActive) {
    // --- Run both motors simultaneously (non‑blocking) ---
    if (!home1Done) {
      track1.runSpeed();
      if (track1.atLimit1()) {
        track1.stop();
        track1.setCurrentPosition(0);
        track1.disable();
        home1Done = true;
        Serial.println("Track 1 homed");
      }
    }

    if (!home2Done) {
      track2.runSpeed();
      if (track2.atLimit1()) {
        track2.stop();
        track2.setCurrentPosition(0);
        track2.disable();
        home2Done = true;
        Serial.println("Track 2 homed");
      }
    }

    // --- Check if both are finished ---
    if (home1Done && home2Done) {
      homingActive = false;                // stop homing
      homingDoneForMode1 = true;          // remember we've homed in this mode1 session
      Serial.println("Both tracks homed. Ready to play.");
    }

    // Do NOT call handleModeChange() or other mode‑related code while homing
    return;   // skip rest of loop
  }

  // -----------------------------------------------------------------
  // 2. Not homing → handle mode changes and other normal operation
  // -----------------------------------------------------------------
  handleModeChange();        // cycle mode on button press
  updateModeLEDs();          // update LEDs according to current mode

  // --- Start homing when we enter Mode 1 and haven't homed yet ---
  if (MODE == 1 && !homingDoneForMode1) {
    // Initialise homing
    track1.enable();
    track2.enable();
    track1.setSpeed(-400);   // reverse direction
    track2.setSpeed(-400);
    track1.stop();           // ensure not already moving
    track2.stop();

    home1Done = false;
    home2Done = false;
    homingActive = true;     // activate homing state machine

    Serial.println("Starting simultaneous homing...");
  }

  // --- Reset the "homing done" flag when we leave Mode 1 ---
  if (MODE != 1) {
    homingDoneForMode1 = false;
  }

  // -----------------------------------------------------------------
  // 3. Mode 2: Pressure‑sensor controlled movement
  // -----------------------------------------------------------------
  if (MODE == 2) {
    // ----- Track 1 controlled by pressure sensor 1 -----
    if (pressSensor1.getVal() < PRESSURE_THRESHOLD) {
      if (!track1.isEnabled()) track1.enable();
      track1.setSpeed(400);      // move forward (positive direction)
      track1.runSpeed();
    } else {
      if (track1.isEnabled()) {
        track1.stop();
        track1.disable();
      }
    }

    // ----- Track 2 controlled by pressure sensor 2 -----
    if (pressSensor2.getVal() < PRESSURE_THRESHOLD) {
      if (!track2.isEnabled()) track2.enable();
      track2.setSpeed(400);
      track2.runSpeed();
    } else {
      if (track2.isEnabled()) {
        track2.stop();
        track2.disable();
      }
    }

    // ----- Win condition: any track reaches position 4000 -----
    if (track1.currentPosition() >= WIN_POSITION || track2.currentPosition() >= WIN_POSITION) {
      Serial.println("Track reached position 4000! Returning to homing mode.");
      
      // Stop and disable both motors
      if (track1.isEnabled()) { track1.stop(); track1.disable(); }
      if (track2.isEnabled()) { track2.stop(); track2.disable(); }

      // Switch to Mode 1 and ensure homing runs immediately
      MODE = 1;
      homingDoneForMode1 = false;   // force homing on next loop
      updateModeLEDs();            // update LEDs to show Mode 1
    }
  }
}