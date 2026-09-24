#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Preferences.h>
#include <time.h>
#include <LittleFS.h>
#include "secrets.h"

// ============================================================================
//  CAMBIAR POR NODO ANTES DE FLASHEAR — DEVICE_ID único: power-01, power-02, ...
// ============================================================================
#define DEVICE_ID "power-01"

// ---- Placa: SuperMini S3 (ttyACM0 · USB CDC On Boot: Enabled) ----
// Pines I2C del cableado real de la pinza/ADS1115 (según la diagnóstica que anda).
#define I2C_SDA 9
#define I2C_SCL 10

// ---- Pinza SCT-013 + ADS1115 ----
#define ADS_ADDR     0x48        // ADDR a GND
#define ADS_CHANNEL  0           // A0
#define FULL_SCALE   4.096f      // GAIN_ONE => ±4.096V
#define I_SAMPLES    1000        // muestras por ventana RMS (~1.5s a 860SPS)
#define CAL_A_PER_V  67.0f       // calibrado contra enchufe medidor (pinza sub-leía ~11%)

#define NODE_TYPE "power"
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

Adafruit_ADS1115 ads;
WiFiClientSecure net;
PubSubClient mqtt(net);
Preferences prefs;

static uint32_t seq = 0;
static bool adsOk = false;

// Suavizado EWMA (la corriente puede saltar; alpha 0.3 = promedio de ~3-4 lecturas)
static const float EWMA_ALPHA = 0.3f;
static float currentEwma = 0;
static bool  ewmaInit = false;

// ---- Buffer store-and-forward (cola en RAM + snapshot periodico a LittleFS) ----
struct Reading {
  uint32_t ts;
  uint32_t seq;
  float    current;   // A rms
};

static const int BUF_CAP = 1440;   // ~2h a 5s por muestra
static Reading ring[BUF_CAP];
static int bufHead = 0, bufCount = 0;
static bool bufDirty = false;

void bufPush(const Reading& r) {
  int tail = (bufHead + bufCount) % BUF_CAP;
  ring[tail] = r;
  if (bufCount < BUF_CAP) bufCount++;
  else bufHead = (bufHead + 1) % BUF_CAP;   // cola llena: pisa la mas vieja
  bufDirty = true;
}

void buildPayload(char* out, size_t n, const Reading& r) {
  snprintf(out, n,
           "{\"type\":\"%s\",\"device\":\"%s\",\"seq\":%u,\"ts\":%u,\"current_a\":%.2f}",
           NODE_TYPE, DEVICE_ID, r.seq, r.ts, r.current);
}

void bufDrain() {
  static uint32_t lastDrain = 0;
  if (bufCount == 0 || !mqtt.connected() || millis() - lastDrain < 500) return;
  lastDrain = millis();
  for (int k = 0; k < 2 && bufCount > 0 && mqtt.connected(); k++) {
    char payload[192];
    buildPayload(payload, sizeof(payload), ring[bufHead]);
    if (!mqtt.publish(TOPIC, payload)) break;   // sin ACK: reintenta despues
    bufHead = (bufHead + 1) % BUF_CAP; bufCount--; bufDirty = true;
  }
}

void bufSave() {
  File f = LittleFS.open("/buf.bin", "w");
  if (!f) return;
  f.write((const uint8_t*)&bufCount, sizeof(bufCount));
  for (int i = 0; i < bufCount; i++) {
    Reading& r = ring[(bufHead + i) % BUF_CAP];
    f.write((const uint8_t*)&r, sizeof(Reading));
  }
  f.close();
  bufDirty = false;
}

void bufLoad() {
  File f = LittleFS.open("/buf.bin", "r");
  if (!f) return;
  int cnt = 0;
  if (f.read((uint8_t*)&cnt, sizeof(cnt)) != sizeof(cnt) || cnt < 0 || cnt > BUF_CAP) {
    f.close(); return;
  }
  bufHead = 0; bufCount = 0;
  for (int i = 0; i < cnt; i++) {
    Reading r;
    if (f.read((uint8_t*)&r, sizeof(Reading)) != sizeof(Reading)) break;
    ring[i] = r; bufCount++;
    if (r.seq > seq) seq = r.seq;
  }
  f.close();
  Serial.printf("buffer recuperado de flash: %d lecturas\n", bufCount);
}

const time_t TIME_FLOOR = 1700000000;

void adsInit() {
  Wire.begin(I2C_SDA, I2C_SCL);
  if (ads.begin(ADS_ADDR)) {
    ads.setGain(GAIN_ONE);                 // ±4.096V
    ads.setDataRate(RATE_ADS1115_860SPS);
    adsOk = true;
    Serial.println("ADS1115 OK (0x48). Sin corriente el offset DC deberia dar ~1.65V.");
  } else {
    Serial.println("ADS1115 NO responde en 0x48 (revisa I2C/ADDR/cableado)");
  }
}

// RMS de la parte AC: saca el bias (~1.65V) por varianza; convierte a amperes.
float readIrms() {
  if (!adsOk) return 0.0f;
  double sum = 0, sumSq = 0;
  for (int i = 0; i < I_SAMPLES; i++) {
    int16_t raw = ads.readADC_SingleEnded(ADS_CHANNEL);
    float v = raw * (FULL_SCALE / 32768.0f);
    sum   += v;
    sumSq += (double)v * v;
  }
  float mean = sum / I_SAMPLES;
  float var  = (float)(sumSq / I_SAMPLES - (double)mean * mean);   // = Vrms_ac^2
  if (var < 0) var = 0;
  float vrms = sqrtf(var);
  return vrms * CAL_A_PER_V;
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("WiFi");
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);          // el S3 a veces no asocia si no se fija STA explícito
  WiFi.setSleep(false);         // modem-sleep off: en el S3 arruina el handshake TLS (rc=-2)
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
  // No bloquear el arranque headless (S3 USB nativo): espera <=2s por el Serial.
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 2000; ) delay(10);

  adsInit();

  prefs.begin("telemetry", false);
  seq = prefs.getUInt("seq", 0);

  if (!LittleFS.begin(true)) Serial.println("LittleFS no monta (buffer solo en RAM)");
  else bufLoad();

  net.setCACert(ROOT_CA);
  net.setHandshakeTimeout(30);   // más margen para el handshake TLS en el S3
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(256);

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
  if (millis() - lastPub >= 5000) {
    lastPub = millis();

    float irms = readIrms();   // bloquea ~1.5s (1000 muestras a 860SPS)
    currentEwma = ewmaInit ? (EWMA_ALPHA * irms + (1.0f - EWMA_ALPHA) * currentEwma) : irms;
    ewmaInit = true;

    Reading r;
    r.ts = (uint32_t)time(nullptr);
    r.seq = ++seq;
    r.current = currentEwma;
    prefs.putUInt("seq", seq);
    bufPush(r);

    Serial.printf("Irms=%.2f(->%.2f) A  enq (cola=%d)\n", irms, currentEwma, bufCount);
  }

  bufDrain();
  static uint32_t lastSnap = 0;
  if (bufDirty && millis() - lastSnap >= 300000) {
    lastSnap = millis();
    bufSave();
  }
}
