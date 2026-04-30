#include "Arduino.h"
#include "DFRobotDFPlayerMini.h"

const int potPin = A6;
const int rxPin = 3;     // ESP32 RX -> DFPlayer TX
const int txPin = 2;     // ESP32 TX -> DFPlayer RX
const int busyPin = 4;   // NEW: Connect to DFPlayer BUSY pin

DFRobotDFPlayerMini myDFPlayer;
bool startupFinished = false;

void setup() {
  Serial.begin(115200);
  pinMode(busyPin, INPUT); // Initialize the busy pin

  Serial1.begin(9600, SERIAL_8N1, rxPin, txPin);

  if (!myDFPlayer.begin(Serial1)) {
    Serial.println(F("DFPlayer not found."));
    while(true); 
  }

  updateVolume();

  Serial.println(F("Playing startup track: 12"));
  myDFPlayer.play(12); 
  
  // Wait 2 seconds to let the BUSY pin go LOW
  delay(2000); 
}

void loop() {
  updateVolume();

  // digitalRead(busyPin) == HIGH means the player is NOT playing
  if (digitalRead(busyPin) == HIGH) {
    int randomTrack = random(1, 12); 
    Serial.print(F("Playing random track: "));
    Serial.println(randomTrack);
    
    myDFPlayer.play(randomTrack);
    
    // CRITICAL: Wait for the DFPlayer to actually start and pull the Busy pin LOW
    // Otherwise, the loop will run again instantly and pick another track.
    delay(2000); 
  }

  delay(500); 
}

void updateVolume() {
  int potValue = analogRead(potPin);
  int volume = map(potValue, 0, 4095, 0, 30);

  static int lastVolume = -1;
  if (volume != lastVolume) {
    myDFPlayer.volume(volume);
    lastVolume = volume;
  }
}