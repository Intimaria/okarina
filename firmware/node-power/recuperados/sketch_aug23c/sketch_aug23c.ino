// Diagnóstico corriente — variante con escáner I2C (2026-08-23 15:26).
// Recuperado del cache de Arduino IDE (temp unsaved). Archivo histórico.
// Es la que se usó para depurar el ADS1115 (escanea el bus I2C y prueba pines).
// Ya usa 9/10 y el método bueno (varianza = RMS de la parte AC, DC dinámico).
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// ---- Configuración: cambiá solo estas líneas si hace falta ----
#define SDA_PIN 9          // probá 9 si en 6/7 no aparece nada
#define SCL_PIN 10         // probá 10 si en 6/7 no aparece nada
const uint8_t ADS_ADDR = 0x48;   // 0x48 = ADDR a GND
const float FULL_SCALE = 4.096;  // rango con GAIN_ONE (±4.096V)
const int   SAMPLES    = 1000;
const float CAL_A_PER_V = 60.0;  // 60A -> 1V (se ajusta luego con carga real)
// ---------------------------------------------------------------

Adafruit_ADS1115 ads;
bool ads_ok = false;

void scanI2C() {
  Serial.println("Escaneando bus I2C...");
  int n = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("  -> dispositivo en 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
      n++;
    }
  }
  if (n == 0) Serial.println("  -> NO se encontro ningun dispositivo I2C");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Wire.begin(SDA_PIN, SCL_PIN);
  Serial.println("\n=== Diagnostico ADS1115 ===");

  scanI2C();

  if (ads.begin(ADS_ADDR)) {
    ads.setGain(GAIN_ONE);                 // ±4.096V
    ads.setDataRate(RATE_ADS1115_860SPS);
    ads_ok = true;
    Serial.println("ADS1115 OK. Sin corriente el offset DC deberia dar ~1.65 V.");
  } else {
    Serial.println("ADS1115 NO responde en esa direccion.");
    Serial.println("Revisa: 1) ADDR->GND  2) VDD=3.3V  3) probar pines 9/10  4) cruzar SDA/SCL");
  }
}

void loop() {
  if (!ads_ok) { delay(2000); return; }

  // Un solo barrido: calculamos media (offset DC real) y RMS de la parte AC.
  double sum = 0, sumSq = 0;
  for (int i = 0; i < SAMPLES; i++) {
    int16_t raw = ads.readADC_SingleEnded(0);      // canal A0
    float v = raw * (FULL_SCALE / 32768.0);
    sum   += v;
    sumSq += v * v;
  }
  float mean = sum / SAMPLES;                       // offset DC medido
  float variance = (sumSq / SAMPLES) - (mean * mean);
  if (variance < 0) variance = 0;
  float vrms = sqrt(variance);                      // RMS de la señal AC
  float irms = vrms * CAL_A_PER_V;

  Serial.print("Offset DC = ");
  Serial.print(mean, 3);
  Serial.print(" V   Irms = ");
  Serial.print(irms, 3);
  Serial.println(" A");

  delay(300);
}
