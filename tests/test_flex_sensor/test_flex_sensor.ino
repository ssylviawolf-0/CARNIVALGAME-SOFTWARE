// Basic FSR / flex-sensor reading sketch
// Wire sensor to A0 with a 10kΩ pull-down resistor for stable readings.

int fsrPin = A0;
int fsrReading;
float voltage;
float force;

void setup() {
  Serial.begin(9600);
  Serial.println("FSR Pressure Sensor Ready");
}

void loop() {
  fsrReading = analogRead(fsrPin);

  voltage = fsrReading * (5.0 / 1023.0);
  force    = map(fsrReading, 0, 1023, 0, 500);

  Serial.print("Raw: ");     Serial.print(fsrReading);
  Serial.print(" | V: ");    Serial.print(voltage);
  Serial.print(" | Force: "); Serial.print(force);
  Serial.println("g");

  delay(100);
}
