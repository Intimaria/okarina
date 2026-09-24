#include <Wire.h>

// SCD41 en I2C: SDA=GPIO8, SCL=GPIO9. Debe responder en 0x62.

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(8, 9);
  Serial.println("i2c-scan: escaneando bus (SDA=8, SCL=9)...");
}

void loop() {
  int found = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("  encontrado: 0x");
      Serial.println(addr, HEX);
      found++;
    }
  }
  if (!found) Serial.println("  nadie contesta - revisar SDA/SCL y pull-ups");
  Serial.println("--- fin del escaneo, repito en 3s ---");
  delay(3000);
}
