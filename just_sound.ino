#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>


SoftwareSerial mp3Serial(10, 11);   // RX=D10 (to DF TX), TX=D11 (to DF RX)
DFRobotDFPlayerMini mp3;


void setup() {
  mp3Serial.begin(9600);


  if (!mp3.begin(mp3Serial)) {
    while (1) {}  // wiring/SD issue
  }


  mp3.outputDevice(DFPLAYER_DEVICE_SD); // be explicit
  mp3.volume(7);
  mp3.EQ(DFPLAYER_EQ_BASS);


  mp3.play(1);                 // start playback first (works on all units)
  delay(200);                  // give it a moment to start
  mp3.enableLoopAll();         // ask firmware to loop all


  // Optional: if your unit ignores loop-all, this event loop is a tiny fallback
}


void loop() {
  if (mp3.available()) {
    if (mp3.readType() == DFPlayerPlayFinished) {
      // Firmware ignored loop-all -> advance manually
      static uint16_t n = 1;
      n = (n % 255) + 1;       // naive next; or track count if you want to bound it
      mp3.play(n);
    }
  }
}
