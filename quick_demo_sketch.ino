#include <SoftwareSerial.h>
#include <TMCStepper.h>
#include <AccelStepper.h>

//////////////////////////////////////////////////////////////////////
#include <FastLED.h>
//PWR->SCL22, GND->53

#define LED_PIN     A12
#define LED_COUNT   40
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
CRGB leds[LED_COUNT];


#define BRIGHTNESS  128


uint8_t currentPattern = 0;
const uint8_t totalPatterns = 11;   // how many patterns you have
unsigned long lastSwitch = 0;
const unsigned long patternInterval = 5000; // change every 5 seconds
///////////////////////////////////////////////////////////////////////

// Define pin connections for Motor 1
#define STEP_PIN1 3
#define DIR_PIN1 2
#define ENABLE_PIN1 7
#define SW_RX1 5    // Arduino RX ← TMC2209 #1 TX (PDN_UART)
#define SW_TX1 6    // Arduino TX → TMC2209 #1 RX (DIAG1)

// Define pin connections for Motor 2  
#define STEP_PIN2 9
#define DIR_PIN2 8
#define ENABLE_PIN2 13
#define SW_RX2 11   // Arduino RX ← TMC2209 #2 TX (PDN_UART)
#define SW_TX2 12   // Arduino TX → TMC2209 #2 RX (DIAG1)

#define Flex_pin1 A0
#define Flex_pin2 A1

#define Pressure_pin1 A2
#define Pressure_pin2 A3

const int EndTrack = 14000;
int Subdivisions = 5;
int steps = (EndTrack/Subdivisions);
bool Resetting = false;

int flex1val;
int pres1val;
int flex2val;
int pres2val;
const int minflexval = 800;
const int maxpresval = 400;
bool OneCurrStanding = false;
bool OnePrevStanding = false;
bool TwoCurrStanding = false;
bool TwoPrevStanding = false;
bool OneStood = false;
bool TwoStood = false;
unsigned long onecurr = 0;
unsigned long oneprev = 0;
unsigned long twocurr = 0;
unsigned long twoprev = 0;
const int polling = 10;

// Create SoftwareSerial objects for both drivers
SoftwareSerial TMC2209_Serial1(SW_RX1, SW_TX1);
SoftwareSerial TMC2209_Serial2(SW_RX2, SW_TX2);

// Create TMC2209 driver objects with different addresses
TMC2209Stepper driver1(&TMC2209_Serial1, 0.11f, 0b00);  // Address 0b00
TMC2209Stepper driver2(&TMC2209_Serial2, 0.11f, 0b01);  // Address 0b01

// Create AccelStepper objects
AccelStepper stepper1(AccelStepper::DRIVER, STEP_PIN1, DIR_PIN1);
AccelStepper stepper2(AccelStepper::DRIVER, STEP_PIN2, DIR_PIN2);

void setupDriver(TMC2209Stepper &driver, const char* driverName) {
  Serial.print("Initializing ");
  Serial.print(driverName);
  Serial.println("...");
  
  // Test communication
  Serial.print(driverName);
  Serial.print(" connection: ");
  bool connection = driver.test_connection();
  Serial.println(connection ? "SUCCESS" : "FAILED");
  
  if (connection) {
    Serial.print("Configuring ");
    Serial.println(driverName);
    
    // Configure driver settings
    driver.rms_current(600);      // 600mA current
    driver.microsteps(8);         // 2x microstepping
    driver.toff(5);               // Enable driver
    driver.pwm_autoscale(true);   // Enable StealthChop
    driver.en_spreadCycle(false); // Disable SpreadCycle
    
    // Note: Removed driver.push() as it was causing issues
    
    delay(100);
    
    // Verify settings (readback might be unreliable but try anyway)
    Serial.println("=== Driver Configuration ===");
    Serial.print(driverName);
    Serial.print(" Microsteps: ");
    Serial.println(driver.microsteps());
    Serial.print(driverName);
    Serial.print(" Current: ");
    Serial.println(driver.rms_current());
    
    Serial.print(driverName);
    Serial.println(" configured!");
  }
  
  Serial.println();
}

void setup() {
  ///////////////////////////////////////////////////////////////////////
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, LED_COUNT);
  FastLED.setBrightness(BRIGHTNESS);
  random16_add_entropy(analogRead(0));  // random seed
  ///////////////////////////////////////////////////////////////////////

  Serial.begin(9600);
  while (!Serial);
  Serial.println("Dual TMC2209 UART Connection Test");
  Serial.println("==================================");
  
  // Initialize enable pins (LOW enables the drivers)
  pinMode(ENABLE_PIN1, OUTPUT);
  digitalWrite(ENABLE_PIN1, LOW);
  pinMode(ENABLE_PIN2, OUTPUT);
  digitalWrite(ENABLE_PIN2, LOW);
  Serial.println("Both drivers enabled");
  Serial.println();

  pinMode(30, INPUT_PULLUP); // 
  pinMode(48, OUTPUT);
  pinMode(42, OUTPUT);
  pinMode(36, OUTPUT);
  
  // Initialize SoftwareSerial for both TMC2209 drivers
  TMC2209_Serial1.begin(115200);
  TMC2209_Serial2.begin(115200);
  delay(100);
  
  // Initialize both drivers
  driver1.begin();
  driver2.begin();
  delay(100);
  
  // Configure both drivers
  setupDriver(driver1, "Driver 1");
  setupDriver(driver2, "Driver 2");
  
  // Configure AccelStepper parameters
  stepper1.setMaxSpeed(4000);
  stepper1.setAcceleration(1000);
  stepper1.setCurrentPosition(0);
  
  stepper2.setMaxSpeed(4000);
  stepper2.setAcceleration(1000);
  stepper2.setCurrentPosition(0);
  
  Serial.println("Setup complete - testing motor movement...");
  
  int val = 1;
  val = digitalRead(30);

  /*
  while(val == 1){
    val = digitalRead(30);
    delay(10);
  } */
  digitalWrite(48, HIGH);
  delay(400);
  digitalWrite(42, HIGH);
  delay(400);
  digitalWrite(36, HIGH);
  delay(400);

  stepper1.setCurrentPosition(0); //Set the current position as start
  stepper2.setCurrentPosition(0); //Set the current position as start 
}

void standing(int num){
  if(num == 1){
    flex1val = analogRead(Flex_pin1); //Get the sensor values 
    pres1val = analogRead(Pressure_pin1);
    if((flex1val >= minflexval) && (pres1val <= maxpresval)){ //If the pressure sensor is pressed and the flex sensor is unflexed the player is currently standing
      OneCurrStanding = true;
    }
    else { //If not, they are not standing
      OneCurrStanding = false;
    }
    if((OneCurrStanding == true) && (OnePrevStanding == false)){ //We check if the player is currently standing and was not standing before, if this is the case, we say they stood up 
      OneStood = true;
    }
    OnePrevStanding = OneCurrStanding; //We then update our old value
  }
  if(num == 2){
    flex2val = analogRead(Flex_pin2);
    pres2val = analogRead(Pressure_pin2);
    if((flex2val >= minflexval) && (pres2val <= maxpresval)){
      TwoCurrStanding = true;
    }
    else {
      TwoCurrStanding = false;
    }
    if((TwoCurrStanding == true) && (TwoPrevStanding == false)){
      TwoStood = true;
    }
    TwoPrevStanding = TwoCurrStanding;
  }
}

///////////////////////////////////////////////////////////////////////////////////
void rainbowFlow() {
  static uint8_t hue = 0;
  fill_rainbow(leds, LED_COUNT, hue, 255 / LED_COUNT);
  FastLED.show();
  hue++;
}


void chaser() {
  static uint8_t pos = 0;
  fill_solid(leds, LED_COUNT, CRGB::Black);
  leds[pos] = CRGB::Yellow;
  leds[(pos + 1) % LED_COUNT] = CRGB::Orange;
  leds[(pos + 2) % LED_COUNT] = CRGB::Red;
  FastLED.show();
  pos = (pos + 1) % LED_COUNT;
}


void confetti() {
  fadeToBlackBy(leds, LED_COUNT, 10);
  int pos = random16(LED_COUNT);
  leds[pos] += CHSV(random8(), 255, 255);
  FastLED.show();
}


void pulse() {
  static uint8_t brightness = 0;
  static int8_t direction = 1;
  brightness += direction * 4;
  if (brightness == 0 || brightness == 255) direction = -direction;
  fill_solid(leds, LED_COUNT, CHSV(0, 0, brightness)); // white pulse
  FastLED.show();
}
// Sparkle - Random LEDs flash white briefly
void sparkle() {
  fill_solid(leds, LED_COUNT, CRGB::Black);
  int pos = random16(LED_COUNT);
  leds[pos] = CRGB::White;
  FastLED.show();
  delay(50);
}
// Fire - Flickering flame effect
void fire() {
  static byte heat[LED_COUNT];
  // Cool down every cell a little
  for(int i = 0; i < LED_COUNT; i++) {
    heat[i] = qsub8(heat[i], random8(0, ((55 * 10) / LED_COUNT) + 2));
  }
  // Heat from each cell drifts up and diffuses
  for(int k = LED_COUNT - 1; k >= 2; k--) {
    heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
  }
// Randomly ignite new sparks near bottom
  if(random8() < 120) {
    int y = random8(7);
    heat[y] = qadd8(heat[y], random8(160, 255));
  }
  // Convert heat to LED colors
  for(int j = 0; j < LED_COUNT; j++) {
    byte temperature = heat[j];
    byte t192 = scale8_video(temperature, 192);
    byte heatramp = t192 & 0x3F;
    heatramp <<= 2;
    if(t192 & 0x80) {
      leds[j] = CRGB(255, 255, heatramp);
    } else if(t192 & 0x40) {
      leds[j] = CRGB(255, heatramp, 0);
    } else {
      leds[j] = CRGB(heatramp, 0, 0);
    }
  }
  FastLED.show();
}
// Theater Chase - Classic marquee style
void theaterChase() {
  static uint8_t offset = 0;
  static uint8_t hue = 0;
  for(int i = 0; i < LED_COUNT; i++) {
    if((i + offset) % 3 == 0) {
      leds[i] = CHSV(hue, 255, 255);
    } else {
      leds[i] = CRGB::Black;
    }
  }
  FastLED.show();
  offset = (offset + 1) % 3;
  hue += 2;
}
// Comet - A bright tail that travels
void comet() {
  static int pos = 0;
  static uint8_t hue = 0;
  fadeToBlackBy(leds, LED_COUNT, 64);
  leds[pos] = CHSV(hue, 255, 255);
  FastLED.show();
  pos = (pos + 1) % LED_COUNT;
  hue++;
}
// Twinkle - Multiple random LEDs gently fade in and out
void twinkle() {
  fadeToBlackBy(leds, LED_COUNT, 32);
  if(random8() < 80) {
    int pos = random16(LED_COUNT);
    leds[pos] = CHSV(random8(), 200, 255);
  }
  FastLED.show();
}
// Color Wave - Smooth wave of changing colors
void colorWave() {
  static uint8_t offset = 0;
  for(int i = 0; i < LED_COUNT; i++) {
    uint8_t hue = (i * 256 / LED_COUNT) + offset;
    leds[i] = CHSV(hue, 255, 200);
  }
  FastLED.show();
  offset += 2;
}
// Dual Scanner - Two dots bouncing back and forth
void dualScanner() {
  static int pos1 = 0;
  static int pos2 = LED_COUNT / 2;
  static int dir1 = 1;
  static int dir2 = 1;
  
  fadeToBlackBy(leds, LED_COUNT, 64);
  leds[pos1] = CRGB::Red;
  leds[pos2] = CRGB::Blue;
  FastLED.show();
  
  pos1 += dir1;
  pos2 += dir2;
  
  if(pos1 <= 0 || pos1 >= LED_COUNT - 1) dir1 = -dir1;
  if(pos2 <= 0 || pos2 >= LED_COUNT - 1) dir2 = -dir2;
}


// ---- call the right one based on index ----
void runPattern(uint8_t p) {
  switch (p) {
    case 0: rainbowFlow(); break;
    case 1: chaser(); break;
    case 2: confetti(); break;
    case 3: pulse(); break;
    case 4: sparkle(); break;
    case 5: fire(); break;
    case 6: theaterChase(); break;
    case 7: comet(); break;
    case 8: twinkle(); break;
    case 9: colorWave(); break;
    case 10: dualScanner(); break;
  }

}
///////////////////////////////////////////////////////////////////////////////////

void loop() {
  // Run both steppers
  stepper1.run();
  stepper2.run();

  ///////////////////////////////////////////////////////////////////////////////
  EVERY_N_MILLISECONDS(20) {            // refresh animation speed
    runPattern(currentPattern);
  }


  // change pattern every 5 seconds
  if (millis() - lastSwitch > patternInterval) {
    lastSwitch = millis();
    currentPattern = (currentPattern + 1) % totalPatterns;
    fill_solid(leds, LED_COUNT, CRGB::Black); // clean transition
    FastLED.show();
  }
  ///////////////////////////////////////////////////////////////////////////////


  if(Resetting == false){
  //Check if Player1 Standing
  //Make this so it checks every 10 milliseconds
  onecurr = millis();
  if((onecurr - oneprev) >= polling){
    standing(1);
    oneprev = onecurr;
  }

  //Check if Player2 Standing
  //Make this so it checks every 10 milliseconds
  twocurr = millis();
  if((twocurr - twoprev) >= polling){
    standing(2);
    twoprev = twocurr;
  }
  
  // Motor 1 movement logic
    if(OneStood == true){
      OneStood = false;
      if(stepper1.distanceToGo() == 0){
        stepper1.move(steps);
      }
    }

    // Motor 2 movement logic
    if(TwoStood == true){
      TwoStood = false;
      if(stepper2.distanceToGo() == 0){
        stepper2.move(steps);
      }
    }

    //Check if Either Plate has made it to the end of their tracks (if so, bring both back to start)
    if(stepper1.currentPosition() >= EndTrack){
      //Reset Tracks
      stepper1.moveTo(0);
      stepper2.moveTo(0);
      Resetting = true;

      //Change LED STRIP Lights

    }
    else if(stepper2.currentPosition() >= EndTrack){
      //Reset Tracks
      stepper1.moveTo(0);
      stepper2.moveTo(0);
      Resetting = true;

      //Change LED STRIP Lights

    }
  }

  if (Resetting == true) {
    if (stepper1.distanceToGo() == 0 && stepper2.distanceToGo() == 0){
      Resetting = false;
    }
  }

}
