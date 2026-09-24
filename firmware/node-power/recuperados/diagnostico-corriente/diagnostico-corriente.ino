// Sketch de DIAGNÓSTICO del canal de corriente (SCT-013 + ADS1115).
// Recuperado del cache de Arduino IDE (temp unsaved sketch_aug24a, 2026-08-24)
// tras haberse perdido sin guardar. Es lo que está cargado en la placa: NO
// publica a MQTT, solo imprime por Serial min/max/pp/DC/Irms para validar el
// canal analógico. La versión que publica a MQTT es node-power.ino.
//
// Pinza en A0 del ADS con bias a ~1.65V (divisor 2x10k) y cap de acople.
// Irms = RMS de la parte AC (varianza, sin DC) * CAL_A_PER_V.

#include <Wire.h>
#include <Adafruit_ADS1X15.h>

#define SDA_PIN 9
#define SCL_PIN 10
const uint8_t ADS_ADDR = 0x48;
const float FULL_SCALE = 4.096;   // GAIN_ONE -> ±4.096V
const int   SAMPLES    = 1000;
const float CAL_A_PER_V = 60.0;   // calibración pinza (revisar: BAW sugiere ~67)

Adafruit_ADS1115 ads;
bool ads_ok = false;

void setup() {
  Serial.begin(115200);
  delay(300);
  Wire.begin(SDA_PIN, SCL_PIN);
  if (ads.begin(ADS_ADDR)) {
    ads.setGain(GAIN_ONE);
    ads.setDataRate(RATE_ADS1115_860SPS);
    ads_ok = true;
    Serial.println("ADS1115 OK");
  } else {
    Serial.println("ADS1115 NO responde");
  }
}

void loop() {
  if (!ads_ok) { delay(2000); return; }

  double sum = 0, sumSq = 0;
  int16_t rmin = 32767, rmax = -32768;
  for (int i = 0; i < SAMPLES; i++) {
    int16_t raw = ads.readADC_SingleEnded(0);
    float v = raw * (FULL_SCALE / 32768.0);
    sum += v; sumSq += v * v;
    if (raw < rmin) rmin = raw;
    if (raw > rmax) rmax = raw;
  }
  float mean = sum / SAMPLES;
  float variance = (sumSq / SAMPLES) - (mean * mean);
  if (variance < 0) variance = 0;
  float irms = sqrt(variance) * CAL_A_PER_V;

  float vmin = rmin * (FULL_SCALE / 32768.0);
  float vmax = rmax * (FULL_SCALE / 32768.0);

  Serial.print("min=");  Serial.print(vmin, 3);
  Serial.print(" max="); Serial.print(vmax, 3);
  Serial.print(" pp=");  Serial.print(vmax - vmin, 3);   // pico a pico
  Serial.print(" DC=");  Serial.print(mean, 3);
  Serial.print(" Irms="); Serial.println(irms, 3);
  delay(300);
}
