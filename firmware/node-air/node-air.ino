#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Preferences.h>
#include <time.h>
#include "secrets.h"

// MQ135 — gases (ADC). Alimentado a 5V con divisor 2:1 (2x 10k) + cap 100nF.
#define MQ135_PIN 3        // ADC1_CH6 (pin input-only)
#define MQ135_RL_KOHM 1.0f  // resistor de carga del breakout (SMD 102 = 1k)
#define MQ135_VCC 5.0f      // alimentación del módulo
#define MQ135_DIVIDER 2.0f  // divisor 2:1 antes del ADC

// DHT22 — temp / humedad
#define DHTPIN 10
#define DHTTYPE DHT22

#define NODE_TYPE "air"
#define TOPIC "iot/" NODE_TYPE "/" DEVICE_ID "/telemetry"

// Let's Encrypt — ISRG Root X1 (ancla de la cadena del broker).
// OJO: este es el CA PÚBLICO de Let's Encrypt, NO el certificado del broker.
// Bajarlo de:   curl -s https://letsencrypt.org/certs/isrgrootx1.pem
// o extraerlo del propio broker:
//   openssl s_client -connect <broker-host>:443 -showcerts </dev/null
// (el último certificado de la cadena es la raíz). Si el broker cambia de CA,
// hay que reflashear con el nuevo.
static const char ROOT_CA[] = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)EOF";

DHT dht(DHTPIN, DHTTYPE);
WiFiClientSecure net;
PubSubClient mqtt(net);
Preferences prefs;

static uint32_t seq = 0;
const time_t TIME_FLOOR = 1700000000;

// MQ135 multi-gas. OJO: el sensor NO distingue gases — todas las curvas salen
// del MISMO Rs, asi que se mueven juntas. Es un display mas rico, no medicion
// real de 5 gases. Cada gas tiene su recta log-log del datasheet:
//   log10(Rs/R0) = A*log10(ppm) + B  ->  ppm = 10^((log10(Rs/R0) - B) / A)
// Coeficientes A/B tomados del TP2 del curso (aprox. del grafico del datasheet).
// baseline = resta para "encerar" cada gas en aire limpio (el MQ135 no distingue
// gases: el CO2 natural ~400 ppm baja el Rs y las otras curvas "ven"
// concentraciones irreales). floorv = piso tras la resta. Valores de
// esp32-p1/TP2_MQTT: co2 no se resta pero se pisa en 400; el resto se encera.
struct GasCurve { const char* field; float a; float b; float baseline; float floorv; };
static const GasCurve GASES[] = {
  { "co2_ppm",     -0.3679f, 0.8819f,   0.0f, 400.0f },
  { "co_ppm",      -0.3396f, 0.6521f, 138.0f,   0.0f },
  { "alcohol_ppm", -0.3154f, 0.7266f, 347.0f,   0.0f },
  { "nh3_ppm",     -0.4103f, 0.8384f, 168.0f,   0.0f },
  { "toluene_ppm", -0.3452f, 0.7915f, 323.0f,   0.0f },
};
static const int NGAS = sizeof(GASES) / sizeof(GASES[0]);

static float mq135_R0 = 10.0f;  // valor inicial; se recalcula en calibrateR0()
static float gasEwma[NGAS];       // valor suavizado que se publica
static bool  ewmaInit = false;
static const float EWMA_ALPHA = 0.3f;  // suavizado ~ ultimos 3-4 publishes

int mq135ReadADC(int samples = 10) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(MQ135_PIN);
    delay(2);
  }
  return sum / samples;
}

float mq135RsFromADC(int adc) {
  float vadc = adc / 4095.0f * 3.3f;      // voltaje en el ADC
  float ao = vadc * MQ135_DIVIDER;         // reconstruyo AO real (divisor 2:1)
  if (ao >= MQ135_VCC - 0.05f) ao = MQ135_VCC - 0.05f;
  return MQ135_RL_KOHM * (MQ135_VCC - ao) / ao;  // Rs en kOhm
}

float mq135ToGasPPM(float rs, float a, float b) {
  float ratio = rs / mq135_R0;
  if (ratio <= 0.0f) ratio = 0.001f;
  return pow(10.0f, (log10(ratio) - b) / a);
}

void calibrateR0() {
  // R0 se ancla al CO2: en aire limpio asumimos ~400 ppm.
  float ratio_clean_air = pow(10.0f, GASES[0].a * log10(400.0f) + GASES[0].b);
  float rs = mq135RsFromADC(mq135ReadADC(50));
  mq135_R0 = rs / ratio_clean_air;
  // R0 se autocalibra: sea cual sea el Rs en aire limpio, R0 hace que ese aire
  // de ~400 ppm de CO2 y encere el resto. NO forzamos un default: el 30k de
  // TP2_MQTT asumia un MQ135 "normal", pero este sensor tiene R0 real ~0.4k
  // (RL del breakout / sin burn-in) y ese default rompia toda la escala.
  // Solo evitamos division por cero.
  if (mq135_R0 <= 0.0f) mq135_R0 = 0.1f;
  Serial.print("MQ135 R0 = "); Serial.print(mq135_R0); Serial.println(" kOhm");
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("WiFi");
  // Esta placa es ESP32-C3: radio floja y picos de corriente que hacen brownout
  // con el heater del MQ135 a 5V. Fijar modo STA y bajar la potencia de TX reduce
  // el pico y permite asociar (mismo fix que usamos en node-co2 para la C3).
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300); Serial.print(".");
    if (millis() - t0 > 20000) { Serial.println(" timeout -> reboot"); ESP.restart(); }
  }
  Serial.print(" ok RSSI="); Serial.println(WiFi.RSSI());
}

void syncTime() {
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  Serial.print("NTP");
  uint32_t t0 = millis();
  while (time(nullptr) < TIME_FLOOR) {
    delay(300); Serial.print(".");
    if (millis() - t0 > 20000) { Serial.println(" timeout -> reboot"); ESP.restart(); }
  }
  Serial.print(" ok epoch="); Serial.println((long)time(nullptr));
}

void connectMQTT() {
  uint32_t t0 = millis();
  while (!mqtt.connected()) {
    Serial.print("MQTT...");
    if (mqtt.connect(DEVICE_ID, MQTT_USER, MQTT_PASS)) { Serial.println(" ok"); return; }
    Serial.print(" rc="); Serial.print(mqtt.state());
    if (millis() - t0 > 30000) { Serial.println(" timeout -> reboot"); ESP.restart(); }
    delay(1500);
  }
}

void setup() {
  Serial.begin(115200);
  // No bloquear el arranque headless si esta placa se migra a USB nativo (S3).
  // En el DevKit clasico (CP2102) `Serial` ya da true, asi que esto es no-op.
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 2000; ) delay(10);

  dht.begin();
  pinMode(MQ135_PIN, INPUT);

  prefs.begin("telemetry", false);
  seq = prefs.getUInt("seq", 0);

  net.setCACert(ROOT_CA);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(384);

  Serial.print("Calentando MQ135...");
  delay(30000);          // warm-up del heater (corto para prototipo)
  Serial.println(" listo");
  calibrateR0();

  connectWiFi();
  syncTime();
  connectMQTT();

  Serial.println("listo: " TOPIC);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  static uint32_t lastPub = 0;
  if (millis() - lastPub >= 10000) {
    lastPub = millis();

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    int airRaw = mq135ReadADC(10);
    float rs = mq135RsFromADC(airRaw);

    // ppm por gas desde el mismo Rs, con baseline (encerado en aire limpio) y piso,
    // igual que esp32-p1/TP2_MQTT + suavizado EWMA.
    for (int i = 0; i < NGAS; i++) {
      float ppm = mq135ToGasPPM(rs, GASES[i].a, GASES[i].b) - GASES[i].baseline;
      if (ppm < GASES[i].floorv) ppm = GASES[i].floorv;
      gasEwma[i] = ewmaInit ? (EWMA_ALPHA * ppm + (1.0f - EWMA_ALPHA) * gasEwma[i]) : ppm;
    }
    ewmaInit = true;

    // fragmento JSON con los gases: ,"co2_ppm":..,"co_ppm":..,...
    char gbuf[160]; int goff = 0;
    for (int i = 0; i < NGAS; i++) {
      goff += snprintf(gbuf + goff, sizeof(gbuf) - goff,
                       ",\"%s\":%.1f", GASES[i].field, gasEwma[i]);
    }

    char tbuf[16] = "";
    char hbuf[16] = "";
    if (!isnan(t)) snprintf(tbuf, sizeof(tbuf), ",\"temp_c\":%.1f", t);
    if (!isnan(h)) snprintf(hbuf, sizeof(hbuf), ",\"hum_pct\":%.1f", h);

    seq++;
    uint32_t ts = (uint32_t)time(nullptr);
    char payload[320];
    snprintf(payload, sizeof(payload),
             "{\"type\":\"%s\",\"device\":\"%s\",\"seq\":%u,\"ts\":%u,"
             "\"air_raw\":%d%s%s%s}",
             NODE_TYPE, DEVICE_ID, seq, ts, airRaw, gbuf, tbuf, hbuf);
    bool ok = mqtt.publish(TOPIC, payload);
    if (ok) prefs.putUInt("seq", seq);

    Serial.printf("air_raw=%d co2=%.0f co=%.0f alc=%.0f nh3=%.0f tol=%.0f",
                  airRaw, gasEwma[0], gasEwma[1], gasEwma[2], gasEwma[3], gasEwma[4]);
    if (!isnan(t)) Serial.printf(" T=%.1f", t); else Serial.print(" T=nan");
    if (!isnan(h)) Serial.printf(" RH=%.1f", h); else Serial.print(" RH=nan");
    Serial.printf(" sent=%d\n", ok ? 1 : 0);
  }
}
