// Diagnóstico corriente — variante temprana (2026-08-23 15:00).
// Recuperado del cache de Arduino IDE (temp unsaved). Archivo histórico.
// Diferencias vs la final: pines I2C 6/7 (después se pasó a 9/10), y resta un
// V_REF FIJO de 1.65V en vez de medir el DC real (la final usa varianza).
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

Adafruit_ADS1115 ads;

const int SDA_PIN = 6;
const int SCL_PIN = 7;
const float V_REF = 1.65;        // centro del divisor (mitad de 3.3V)
const float FULL_SCALE = 4.096;  // rango con gain=1
const int SAMPLES = 500;
const float CAL_A_PER_V = 60.0;  // 60A -> 1V (ajustar con carga real)

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  if (!ads.begin(0x48)) {
    Serial.println("ADS1115 no encontrado");
    while (1) {}
  }

  ads.setGain(GAIN_ONE);
  ads.setDataRate(RATE_ADS1115_860SPS);
  Serial.println("Listo. Sin corriente deberia leer ~1.65 V.");
}

void loop() {
  int16_t raw = ads.readADC_SingleEnded(0);
  float dc = raw * (FULL_SCALE / 32768.0);

  float sum = 0;
  for (int i = 0; i < SAMPLES; i++) {
    int16_t r = ads.readADC_SingleEnded(0);
    float v = r * (FULL_SCALE / 32768.0);
    float ac = v - V_REF;
    sum += ac * ac;
  }
  float irms = sqrt(sum / SAMPLES) * CAL_A_PER_V;

  Serial.print("DC = ");
  Serial.print(dc, 3);
  Serial.print(" V   Irms = ");
  Serial.print(irms, 3);
  Serial.println(" A");

  delay(200);
}
