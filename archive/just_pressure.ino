

#include <SoftwareSerial.h>
#include <TMCStepper.h>
#include <AccelStepper.h>

//CUSTOM PIN NAMES----------------------------------------------------------------------
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

  // Connect TMC2209 DIAG pins to these Arduino pins
  #define STALL_PIN1 4
  #define STALL_PIN2 10

  //Pins used for sensor inputs 
  #define PRES1 A0
  #define PRES2 A1

  //Pins used for limit switches on track 1
  #define TRK1_LS1 A2
  #define TRK1_LS2 A3

  //Pins used for limit switches on track 2
  #define TRK2_LS1 A4
  #define TRK2_LS2 A5

  //Pin used for start button
  #define STRT_PIN A6

//CUSTOM CONSTANTS----------------------------------------------------------------------
  #define PRESSURE_THRESHOLD 500   // analog value below this is considered "low"
  #define NUM_LEDS 30              // number of LEDs in the strip

//CUSTOM VARIABLES----------------------------------------------------------------------
  bool gameOn = false;          // true when game is running
  int currentMode = 0;          // 0,1,2,3 – used for mode-specific behaviour
  bool modeJustChanged = false;   // becomes true for one loop after a mode change

//CUSTOM CLASSES------------------------------------------------------------------------
  class MyLimitSwitch {
    public:
      MyLimitSwitch(int pinNum) {
        pin = pinNum;
        pinMode(pin, INPUT_PULLUP);
        previousState = digitalRead(pin);
      }

      // Returns true only on a falling edge (HIGH → LOW)
      bool isPressed() {
        bool currentState = digitalRead(pin);
        bool pressed = (previousState == HIGH && currentState == LOW);
        previousState = currentState;
        return pressed;
      }

      // Returns true if the switch is currently pressed (LOW)
      bool isCurrentlyPressed() {
        return digitalRead(pin) == LOW;
      }

    private:
      int pin;
      bool previousState;
  };

  class MyButton {
    public:
      // Constructor: sets the pin as INPUT_PULLUP and initialises state tracking
      MyButton(int pinNum) {
        pin = pinNum;
        pinMode(pin, INPUT_PULLUP);
        lastState = digitalRead(pin);
        pressStartTime = 0;
        holdTriggered = false;
      }

      // Returns true if the button is currently pressed (LOW)
      bool isPressed() {
          return digitalRead(pin) == LOW;
      }

      // Returns true ONCE when the button has been held for at least 'num' seconds
      bool holdTimeOnce(int num) {
        unsigned long threshold = num * 1000UL;
        bool currentState = digitalRead(pin);
        bool pressed = (currentState == LOW);

        // Detect falling edge → button just pressed
        if (pressed && (lastState == HIGH)) {
            pressStartTime = millis();
            holdTriggered = false;
        }
        // Detect rising edge → button released
        else if (!pressed && (lastState == LOW)) {
            pressStartTime = 0;
            holdTriggered = false;
        }

        lastState = currentState;

        // If button is still pressed and we haven't triggered yet, check hold time
        if (pressed && !holdTriggered && pressStartTime != 0) {
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

class MyPressureplate {
    public:
      // Constructor: stores the pin number and sets it as INPUT
      MyPressureplate(int pinNum) {
        pin = pinNum;
        pinMode(pin, INPUT);   // although analog pins default to input, it's explicit
      }

      // Returns the current raw analog value (0–1023) from the pressure sensor
      int rawVal() {
        return analogRead(pin);
      }

      /*
      * int currMass()
      * Gives estimate of current mass on plate.
      * Updates once every two seconds to reduce process load.
      * The conversion equation is to be determined later.
      * For now, this method is left empty (returns 0).
      */
      int currMass() {
        // To be implemented when calibration data is available
        return 0;
      }

    private:
      int pin;   // analog pin number
  };
class MyTrack {
    public:
      // Constructor: initialises pins, limit switches, stepper driver, and AccelStepper
      MyTrack(int enPin, int stepPin, int dirPin, int sw_rxPin, int sw_txPin, 
              int startPin, int endPin, int stallPin)
        : startLS(startPin),                          // create limit switch objects
          endLS(endPin),
          swSerial(sw_rxPin, sw_txPin),               // SoftwareSerial for TMC driver
          driver(&swSerial, 0.11f, 0),                 // FIXED: Added address parameter (0 = default)
          stepper(AccelStepper::DRIVER, stepPin, dirPin)  // Initialize AccelStepper
      {
        // Store pins
        this->enPin = enPin;
        this->stallPin = stallPin;
        pinMode(enPin, OUTPUT);
        digitalWrite(enPin, HIGH);      // driver disabled initially (LOW to enable)
        
        // Setup stall detection pin (DIAG from TMC2209)
        pinMode(stallPin, INPUT_PULLUP);

        // Configure TMC driver
        swSerial.begin(115200);          // common baud rate for TMC2209
        driver.begin();
        
        // StallGuard configuration - MUST be in StealthChop mode
        driver.toff(4);                  // Enable stealthChop
        driver.rms_current(600);          // Set motor current (mA) – adjust as needed
        driver.microsteps(2);            // Microstepping
        driver.en_spreadCycle(0);         // Disable spreadCycle, use stealthChop (required for StallGuard)
        driver.TCOOLTHRS(0xFFFFF);        // Enable StallGuard above this speed (max value = always on)
        driver.SGTHRS(50);                 // Stall threshold (0-255, lower = more sensitive)
        driver.semin(0);                   // Disable CoolStep (interferes with StallGuard)

        // Configure AccelStepper
        stepper.setMaxSpeed(2000);         // steps per second
        stepper.setAcceleration(1000);      // steps per second²
        stepper.setSpeed(1000);             // default speed

        // State variables
        tracklength = 0;
        subdivs = 5;
        stepSize = 0;

        resetting = false;
        findingBounds = false;
        slowResetActive = false;
        findBoundsState = 0;
        
        // Stall detection variables
        stallDetected = false;
        stallReported = false;
        lastStallCheck = 0;
        
        // For rolling average
        for(int i = 0; i < 4; i++) {
          stallReadings[i] = 0;
        }
        stallIndex = 0;
      }

      // Resets track to start: moves towards startLS until triggered, then sets position to zero
      // Returns true only when reset is fully completed (startLS triggered and position zeroed)
      // Reset track to start. If already at start, returns true immediately.
      bool resetTrack() {
        // If we are already at the start limit, zero position and return true
        if (startLS.isCurrentlyPressed()) {
          stepper.stop();
          stepper.setCurrentPosition(0);
          resetting = false;
          stallDetected = false;
          stallReported = false;
          digitalWrite(enPin, HIGH);
          return true;
        }

        if (resetting) {
          // Already resetting: check if we just reached start (edge or current)
          if (startLS.isPressed() || startLS.isCurrentlyPressed()) {
            stepper.stop();
            stepper.setCurrentPosition(0);
            resetting = false;
            stallDetected = false;
            stallReported = false;
            digitalWrite(enPin, HIGH);
            return true;
          }
          
          // Check if we stalled during reset
          if (stallDetected && !stallReported) {
            stallReported = true;
            Serial.println("Warning: Track stall detected during reset");
            // Continue trying to reset? Or abort? We'll continue but warn.
          }
          
          return false;
        } else {
          // Start a new reset only if not busy with other operations
          if (!findingBounds && !slowResetActive && !stepper.isRunning()) {
            resetting = true;
            stallDetected = false;
            stallReported = false;
            digitalWrite(enPin, LOW);
            stepper.moveTo(-1000000);     // move towards start
            stepper.setSpeed(1000);
            stepper.setAcceleration(1000);
            return false;
          }
          return false;
        }
      }

      // Finds bounds: moves to start, zeros, then moves to end and records tracklength
      // Find bounds: if start is already pressed, zero and immediately go to end
      void findBounds() {
        if (resetting || findingBounds || slowResetActive || stepper.isRunning()) return;

        findingBounds = true;
        stallDetected = false;
        stallReported = false;
        
        if (startLS.isCurrentlyPressed()) {
            // Already at start: zero and proceed to end
            stepper.setCurrentPosition(0);
            findBoundsState = 2;          // skip moving to start
            stepper.moveTo(1000000);
            stepper.setSpeed(800);
            digitalWrite(enPin, LOW);
        } else {
            findBoundsState = 1;           // need to move to start first
            stepper.moveTo(-1000000);
            stepper.setSpeed(800);
            digitalWrite(enPin, LOW);
        }
      }

      // Moves the track forward by one subdivision (tracklength / subdivs)
      // Cancels any active slowReset.
      void moveNextPosition() {
        if (tracklength == 0) return;          // bounds not yet found
        if (resetting || findingBounds) return; // busy with higher priority tasks
        if (stallDetected) {
            Serial.println("Cannot move: Stall detected");
            return;
        }

        // Cancel slowReset if active
        if (slowResetActive) {
          slowResetActive = false;
        }

        // Compute step size if not already done
        if (stepSize == 0) {
          stepSize = tracklength / subdivs;
        }

        // Set target to current position + stepSize (move towards end)
        long target = stepper.currentPosition() + stepSize;
        stepper.moveTo(target);
        digitalWrite(enPin, LOW);   // ensure driver enabled
        
        // Reset stall flags for new movement
        stallDetected = false;
        stallReported = false;
      }

      // Slowly brings track back to start, stops when startLS is pressed.
      // Can be overridden by moveNextPosition().
      void slowReset() {   
        if (resetting || findingBounds) return;
        if (stallDetected) {
            Serial.println("Cannot slow reset: Stall detected");
            return;
        }
        if (!slowResetActive && !stepper.isRunning()) {
          slowResetActive = true;
          stallDetected = false;
          stallReported = false;
          digitalWrite(enPin, LOW);
          stepper.moveTo(-1000000);       // move towards start
          stepper.setSpeed(400);           // slow speed
          stepper.setAcceleration(300);
        }
      }

      // Returns true if a stall was detected
      bool isStalled() {
        return stallDetected;
      }
      
      // Returns the current StallGuard value (0-510, lower = more load)
      int getStallValue() {
        // Take rolling average to filter noise
        int sum = 0;
        for(int i = 0; i < 4; i++) {
          sum += stallReadings[i];
        }
        return sum / 4;
      }
      
      // Clear stall flag (call after resolving the stall condition)
      void clearStall() {
        stallDetected = false;
        stallReported = false;
      }
      
      // Adjust stall sensitivity (0-255, lower = more sensitive)
      void setStallThreshold(int threshold) {
        if(threshold >= 0 && threshold <= 255) {
          driver.SGTHRS(threshold);
        }
      }

      // Must be called continuously in loop() to handle motor movement and limit switches
      void run() {
        stepper.run();

        // Check stall detection when motor is running (every 50ms to avoid too many reads)
        if(stepper.isRunning() && millis() - lastStallCheck > 50) {
          lastStallCheck = millis();
          
          // Read StallGuard value (0-510, lower = higher load)
          int sgValue = driver.SG_RESULT();
          
          // Store in rolling buffer
          stallReadings[stallIndex] = sgValue;
          stallIndex = (stallIndex + 1) % 4;
          
          // Calculate average
          int sum = 0;
          for(int i = 0; i < 4; i++) {
            sum += stallReadings[i];
          }
          int avgLoad = sum / 4;
          
          // If load is high (low SG value), stall detected
          // SGTHRS is set to 50, so SG_RESULT below 50 indicates stall
          if(avgLoad < 30) {  // Conservative threshold
            if(!stallDetected) {
              stallDetected = true;
              stepper.stop();
              digitalWrite(enPin, HIGH);
              Serial.println("Stall detected!");
            }
          }
        }

        // Handle limit switch events
        if (resetting) {
            // Check both edge and current pressed to avoid missing a switch that was already active
            if (startLS.isPressed() || startLS.isCurrentlyPressed()) {
                stepper.stop();
                stepper.setCurrentPosition(0);
                resetting = false;
                stallDetected = false;
                stallReported = false;
                digitalWrite(enPin, HIGH);
            }
        }
        else if (findingBounds) {
            if (findBoundsState == 1) {
                if (startLS.isPressed() || startLS.isCurrentlyPressed()) {
                    stepper.stop();
                    stepper.setCurrentPosition(0);
                    findBoundsState = 2;
                    stepper.moveTo(1000000);
                }
            } else if (findBoundsState == 2) {
                if (endLS.isPressed() || endLS.isCurrentlyPressed()) {
                    stepper.stop();
                    tracklength = stepper.currentPosition();
                    stepSize = tracklength / subdivs;
                    findBoundsState = 0;
                    findingBounds = false;
                    stallDetected = false;
                    stallReported = false;
                    digitalWrite(enPin, HIGH);
                    
                    Serial.print("Track bounds found: ");
                    Serial.print(tracklength);
                    Serial.println(" steps");
                }
            }
            
            // Check for stall during bounds finding
            if (stallDetected && !stallReported) {
                stallReported = true;
                Serial.println("Warning: Stall detected during bounds finding");
                // Abort bounds finding
                stepper.stop();
                findingBounds = false;
                digitalWrite(enPin, HIGH);
            }
        }
        else if (slowResetActive) {
            if (startLS.isPressed() || startLS.isCurrentlyPressed()) {
                stepper.stop();
                stepper.setCurrentPosition(0);
                slowResetActive = false;
                stallDetected = false;
                stallReported = false;
                digitalWrite(enPin, HIGH);
            }
            
            // Check for stall during slow reset
            if (stallDetected && !stallReported) {
                stallReported = true;
                Serial.println("Warning: Stall detected during slow reset");
                // Abort slow reset
                stepper.stop();
                slowResetActive = false;
                digitalWrite(enPin, HIGH);
            }
        }
        else {
            // General safety: if any limit is currently pressed, stop motor
            if (startLS.isCurrentlyPressed() || endLS.isCurrentlyPressed()) {
                stepper.stop();
                digitalWrite(enPin, HIGH);
            }
        }

        // Disable driver when idle
        if (!stepper.isRunning() && digitalRead(enPin) == LOW) {
            digitalWrite(enPin, HIGH);
        }
      }
    
      // Add to MyTrack class (public section):
      bool isFindingBounds() {
        return findingBounds;
      }

      bool isAtStart() {
        return startLS.isCurrentlyPressed();
      }

      // Add to MyTrack public section:
      bool isAtEnd() {
        return endLS.isCurrentlyPressed();
      }

      // Also need to expose stepper.isRunning() for movement detection
      bool isMoving() {
        return stepper.isRunning();
      }

      // ========== NEW METHODS FOR ENCAPSULATION ==========
      
      // Get current position (fixes direct access to stepper)
      long getCurrentPosition() {
        return stepper.currentPosition();
      }

      // Drift backward by a specific number of steps at a specific speed
      void driftBack(int steps, int speed) {
        if (resetting || findingBounds || stallDetected) return;
        if (startLS.isCurrentlyPressed()) return;  // Already at start
        
        // Cancel slowReset if active
        if (slowResetActive) {
          slowResetActive = false;
        }
        
        // Calculate target position (don't go past start)
        long currentPos = stepper.currentPosition();
        long targetPos = currentPos - steps;
        if (targetPos < 0) targetPos = 0;
        
        // Only move if we're not already at target
        if (targetPos != currentPos) {
          stepper.moveTo(targetPos);
          stepper.setSpeed(speed);
          digitalWrite(enPin, LOW);  // Enable driver
        }
      }

      // Move to specific position with given speed
      void moveToPosition(long targetPos, int speed) {
        if (resetting || findingBounds || stallDetected) return;
        
        // Don't go past start or end limits
        if (targetPos < 0) targetPos = 0;
        if (tracklength > 0 && targetPos > tracklength) targetPos = tracklength;
        
        // Cancel slowReset if active
        if (slowResetActive) {
          slowResetActive = false;
        }
        
        stepper.moveTo(targetPos);
        stepper.setSpeed(speed);
        digitalWrite(enPin, LOW);  // Enable driver
      }

    private:
      int enPin;
      int stallPin;
      AccelStepper stepper;

      // Limit switches (using your MyLimitSwitch class)
      MyLimitSwitch startLS;
      MyLimitSwitch endLS;

      // TMC driver via SoftwareSerial
      SoftwareSerial swSerial;
      TMC2209Stepper driver;

      // Track geometry
      long tracklength;        // total steps from start to end
      int subdivs;             // number of subdivisions
      long stepSize;           // tracklength / subdivs

      // State flags
      bool resetting;
      bool findingBounds;
      int findBoundsState;     // 0=idle, 1=toStart, 2=toEnd
      bool slowResetActive;
      
      // Stall detection
      bool stallDetected;
      bool stallReported;
      unsigned long lastStallCheck;
      int stallReadings[4];    // Rolling buffer for StallGuard values
      int stallIndex;
  };

//CLASS OBJECTS-------------------------------------------------------------------------
  // Start button
  MyButton startButton(STRT_PIN);

  // Pressure plates
  MyPressureplate pressurePlate1(PRES1);
  MyPressureplate pressurePlate2(PRES2);

  // Tracks (using the defined pins)
  MyTrack track1(ENBL_PIN1, STEP_PIN1, DIR_PIN1, SW_RX1, SW_TX1, TRK1_LS1, TRK1_LS2, STALL_PIN1);
  MyTrack track2(ENBL_PIN2, STEP_PIN2, DIR_PIN2, SW_RX2, SW_TX2, TRK2_LS1, TRK2_LS2, STALL_PIN2);

//SETUP CODE----------------------------------------------------------------------------
  void setup() {
    Serial.begin(9600);
    
    // Initialise game state: off
    setGameOn(false);
    delay(500);
    
    // Blink status button twice to indicate ready for calibration
    Serial.println("Calibration mode - press button to find track bounds");
    
    // Blink the status LED twice
    for (int i = 0; i < 2; i++) {
      statusLED.turnOn();
      delay(300);
      statusLED.turnOff();
      delay(300);
    }
    
    // Wait for button press to begin calibration
    Serial.println("Press start button to begin track calibration...");
    while (!startButton.isPressed()) {
      delay(50);
    }
    
    // Debounce: wait for button release
    delay(200);
    while (startButton.isPressed()) {
      delay(10);
    }
    
    Serial.println("Finding track bounds...");
    
    // Find bounds for both tracks
    track1.findBounds();
    track2.findBounds();
    
    // Wait for both tracks to complete bounds finding
    while (track1.isFindingBounds() || track2.isFindingBounds()) {
      track1.run();
      track2.run();
      delay(10);
    }
    
    Serial.println("Bounds found. Returning to start position...");
    
    // Reset both tracks to start position
    track1.resetTrack();
    track2.resetTrack();
    
    // Wait for both tracks to reach start position
    while (!track1.isAtStart() || !track2.isAtStart()) {
      track1.run();
      track2.run();
      delay(10);
    }
    
    // Both tracks are now at start position - turn status LED on
    statusLED.turnOn();
    Serial.println("Calibration complete! System ready.");
    Serial.println("Press start button to turn on game mode.");
  }


//MODE FUNCTIONS------------------------------------------------------------------------
  void runMode0() {
    // Standby mode: only LED strip pattern 0, no track movement, no pressure plate checks
    if (modeJustChanged) {
      lightStrip.changePattern(0);   // set pattern only once when entering the mode
    }
    // Nothing else happens in this mode
  }

 void runMode1() {
    // SINGLE PLAYER EASY MODE - Only Track 1 moves
    
    // Mode-specific state variables (static so they persist between loop calls)
    static bool gameWon = false;           // True if player has reached the end
    static bool waitingForReset = false;    // True if showing win screen, waiting for button
    static bool isResetting = false;        // True while track is resetting to start
    static bool isMoving = false;           // True while track is moving to next position
    
    // --- INITIALIZATION: Run once when entering the mode ---
    if (modeJustChanged) {
        // Reset all mode-specific state
        gameWon = false;
        waitingForReset = false;
        isResetting = false;
        isMoving = false;
        
        // Set LED strip to idle pattern
        lightStrip.changePattern(1);  // Pattern 1 = smooth color cycle (waiting)
        
        // Ensure track is at start position (should be from setup, but double-check)
        if (!track1.isAtStart()) {
            track1.resetTrack();
        }
        
        Serial.println("Mode 1: Single Player Easy - Step on plate to move");
    }
    
    // --- WIN STATE: Player has reached the end ---
    if (gameWon) {
        // Make sure win pattern is showing - FIXED: using getter
        if (lightStrip.getCurrentPattern() != 3) {
            lightStrip.changePattern(3);  // Pattern 3 = green blink (win)
        }
        
        // Wait for button press to reset and play again
        if (startButton.holdTimeOnce(0)) {  // Short press detected
            waitingForReset = true;
        }
        
        if (waitingForReset && !isResetting) {
            // Start the reset process
            lightStrip.changePattern(5);  // Pattern 5 = red breathing (resetting)
            track1.resetTrack();
            isResetting = true;
        }
        
        if (isResetting) {
            // Check if reset is complete
            if (track1.isAtStart()) {
                // Reset complete - go back to playing state
                gameWon = false;
                waitingForReset = false;
                isResetting = false;
                lightStrip.changePattern(1);  // Back to idle pattern
                Serial.println("Track reset - Ready to play again");
            }
        }
        
        // Skip rest of mode logic while in win state
        return;
    }
    
    // --- NORMAL GAMEPLAY: Track not at end yet ---
    
    // Read pressure plate value
    int pressureVal = pressurePlate1.rawVal();
    bool platePressed = (pressureVal < PRESSURE_THRESHOLD);
    
    // Track movement logic
    if (isMoving) {
        // Currently moving - update pattern and check if movement complete
        lightStrip.changePattern(2);  // Pattern 2 = moving pixel
        
        // Check if track has finished moving
        if (!track1.isMoving()) {
            // Movement complete
            isMoving = false;
            lightStrip.changePattern(1);  // Back to idle pattern
            
            // Check if we've reached the end
            if (track1.isAtEnd()) {
                gameWon = true;
                Serial.println("PLAYER WINS! Press button to play again");
            }
        }
    } 
    else {
        // Not moving - waiting for player input
        lightStrip.changePattern(1);  // Pattern 1 = idle
        
        // Check for plate release (transition from pressed to not pressed)
        static bool lastPlateState = false;
        
        if (!platePressed && lastPlateState) {
            // Player just stepped OFF the plate - trigger a move
            if (!track1.isAtEnd()) {  // Only move if not at end
                track1.moveNextPosition();
                isMoving = true;
                Serial.println("Step detected - moving track");
            }
        }
        
        lastPlateState = platePressed;
    }
  }

 void runMode2() {
    // TWO PLAYER COMPETITIVE MODE - Both tracks move independently
    
    // Mode-specific state variables (static so they persist between loop calls)
    static bool gameWon = false;           // True if either player has reached the end
    static int winner = 0;                  // 0 = no winner, 1 = player 1, 2 = player 2
    static bool waitingForReset = false;    // True if showing win screen, waiting for button
    static bool isResetting = false;        // True while tracks are resetting
    
    static bool track1Moving = false;       // True while track 1 is moving
    static bool track2Moving = false;       // True while track 2 is moving
    static bool track1MoveRequested = false; // True when plate 1 triggers a move
    static bool track2MoveRequested = false; // True when plate 2 triggers a move
    
    // --- INITIALIZATION: Run once when entering the mode ---
    if (modeJustChanged) {
        // Reset all mode-specific state
        gameWon = false;
        winner = 0;
        waitingForReset = false;
        isResetting = false;
        track1Moving = false;
        track2Moving = false;
        track1MoveRequested = false;
        track2MoveRequested = false;
        
        // Set LED strip to idle pattern
        lightStrip.changePattern(1);  // Pattern 1 = smooth color cycle (waiting)
        
        // Ensure both tracks are at start position
        if (!track1.isAtStart()) {
            track1.resetTrack();
        }
        if (!track2.isAtStart()) {
            track2.resetTrack();
        }
        
        Serial.println("Mode 2: Two Player Competitive - First to the end wins!");
    }
    
    // --- WIN STATE: A player has reached the end ---
    if (gameWon) {
        // Set win pattern based on winner - FIXED: using getter
        if (winner == 1 && lightStrip.getCurrentPattern() != 3) {
            lightStrip.changePattern(3);  // Pattern 3 = green blink (Player 1 wins)
            Serial.println("PLAYER 1 WINS!");
        }
        else if (winner == 2 && lightStrip.getCurrentPattern() != 4) {
            lightStrip.changePattern(4);  // Pattern 4 = blue blink (Player 2 wins)
            Serial.println("PLAYER 2 WINS!");
        }
        
        // Wait for button press to reset and play again
        if (startButton.holdTimeOnce(0)) {  // Short press detected
            waitingForReset = true;
        }
        
        if (waitingForReset && !isResetting) {
            // Start the reset process
            lightStrip.changePattern(5);  // Pattern 5 = red breathing (resetting)
            track1.resetTrack();
            track2.resetTrack();
            isResetting = true;
        }
        
        if (isResetting) {
            // Check if both tracks have finished resetting
            if (track1.isAtStart() && track2.isAtStart()) {
                // Reset complete - go back to playing state
                gameWon = false;
                winner = 0;
                waitingForReset = false;
                isResetting = false;
                lightStrip.changePattern(1);  // Back to idle pattern
                Serial.println("Tracks reset - Ready to play again");
            }
        }
        
        // Skip rest of mode logic while in win state
        return;
    }
    
    // --- NORMAL GAMEPLAY: No winner yet ---
    
    // Read pressure plate values
    int pressureVal1 = pressurePlate1.rawVal();
    int pressureVal2 = pressurePlate2.rawVal();
    bool plate1Pressed = (pressureVal1 < PRESSURE_THRESHOLD);
    bool plate2Pressed = (pressureVal2 < PRESSURE_THRESHOLD);
    
    // Track 1 movement logic
    static bool lastPlate1State = false;
    
    if (track1Moving) {
        // Check if track 1 has finished moving
        if (!track1.isMoving()) {
            track1Moving = false;
            track1MoveRequested = false;
            
            // Check if track 1 reached the end
            if (track1.isAtEnd()) {
                gameWon = true;
                winner = 1;
                return;  // Exit immediately on win
            }
        }
    } 
    else if (!gameWon) {
        // Check for plate 1 release (transition from pressed to not pressed)
        if (!plate1Pressed && lastPlate1State) {
            // Player just stepped OFF plate 1 - trigger a move
            if (!track1.isAtEnd()) {  // Only move if not at end
                track1.moveNextPosition();
                track1Moving = true;
                Serial.println("Player 1 step detected - moving track 1");
            }
        }
    }
    lastPlate1State = plate1Pressed;
    
    // Track 2 movement logic
    static bool lastPlate2State = false;
    
    if (track2Moving) {
        // Check if track 2 has finished moving
        if (!track2.isMoving()) {
            track2Moving = false;
            track2MoveRequested = false;
            
            // Check if track 2 reached the end
            if (track2.isAtEnd()) {
                gameWon = true;
                winner = 2;
                return;  // Exit immediately on win
            }
        }
    } 
    else if (!gameWon) {
        // Check for plate 2 release (transition from pressed to not pressed)
        if (!plate2Pressed && lastPlate2State) {
            // Player just stepped OFF plate 2 - trigger a move
            if (!track2.isAtEnd()) {  // Only move if not at end
                track2.moveNextPosition();
                track2Moving = true;
                Serial.println("Player 2 step detected - moving track 2");
            }
        }
    }
    lastPlate2State = plate2Pressed;
    
    // --- LED STRIP PATTERN LOGIC ---
    if (track1Moving || track2Moving) {
        // Either track is moving - show pattern 2
        lightStrip.changePattern(2);  // Pattern 2 = moving pixel
    } else {
        // No tracks moving - show idle pattern
        lightStrip.changePattern(1);  // Pattern 1 = idle
    }
  }

// MAIN CODE----------------------------------------------------------------------------
  void loop() {
    // ==================== GAME OFF ====================
    if (!gameOn) {
        // Check for short press to turn on (using holdTimeOnce with 0.5 seconds)
        if (startButton.holdTimeOnce(0)) {
            setGameOn(true);
            Serial.println("Game ON");
        }
        return;
    }

    // ==================== GAME ON ====================

    // Run essential hardware updates
    track1.run();
    track2.run();
    lightStrip.play();

    // Check for 2-second hold to turn off (highest priority)
    if (startButton.holdTimeOnce(2)) {
        setGameOn(false);
        Serial.println("Game OFF (2 sec hold)");
        return;
    }

    // Check for 1-second hold to change mode
    if (startButton.holdTimeOnce(1)) {
        currentMode = (currentMode + 1) % 4;
        modeJustChanged = true;
        Serial.print("Mode changed to: ");
        Serial.println(currentMode);
        
        updateModeLEDs();
        track1.resetTrack();
        track2.resetTrack();
    }

    // Mode-specific behavior
    switch (currentMode) {
        case 0: runMode0(); break;
        case 1: runMode1(); break;
        case 2: runMode2(); break;
        case 3: runMode3(); break;
    }

    modeJustChanged = false;
  }

