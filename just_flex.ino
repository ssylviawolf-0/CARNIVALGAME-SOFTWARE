//Figure out which libraries we need to make it work for testing
//#include "FlexLibrary.h" #include <Servo.h>
//#include "SparkFun_Displacement_Sensor_Arduino_Library.h" #include <Wire.h>
//Below is sample code from amazon. If you can find a datasheet for the flexsensors, that'd be helpful
//https://www.amazon.com/dp/B08N67X9L5?_encoding=UTF8&psc=1&ref=cm_sw_r_cp_ud_dp_J8W4C3MWABZZAVXB9DP0&ref_=cm_sw_r_cp_ud_dp_J8W4C3MWABZZAVXB9DP0&social_share=cm_sw_r_cp_ud_dp_J8W4C3MWABZZAVXB9DP0
// Basic FSR Reading
int fsrPin = A0;     // FSR connected to analog pin A0
int fsrReading;      // Variable to store analog reading
float voltage;       // Voltage across the FSR
float force;         // Calculated force value

void setup() {
  Serial.begin(9600);
  Serial.println("FSR Pressure Sensor Ready");
}

void loop() {
  fsrReading = analogRead(fsrPin);
  
  // Convert to voltage (0-5V range)
  voltage = fsrReading * (5.0 / 1023.0);
  
  // Convert to approximate force (0-500g range)
  force = map(fsrReading, 0, 1023, 0, 500);
  
  Serial.print("Analog Reading: ");
  Serial.print(fsrReading);
  Serial.print(" | Voltage: ");
  Serial.print(voltage);
  Serial.print("V | Force: ");
  Serial.print(force);
  Serial.println("g");
  
  delay(100);  // 10Hz sampling rate
}

Key Points:

    Use 10kΩ pull-down resistor for stable readings
    Sensor responds in
