#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <SensirionI2cScd4x.h>
#include <Preferences.h>
#include <time.h>
#include <LittleFS.h>
#include "driver/i2s_std.h"
#include "secrets.h"

// ============================================================================
//  ⚠️  CAMBIAR POR NODO ANTES DE FLASHEAR  ⚠️
//  Cada nodo necesita un DEVICE_ID único: co2-01, co2-02, co2-03, ...
// ============================================================================
#define DEVICE_ID "co2-02"

// ---- Selección de placa: cambiá SOLO la línea `#define BOARD ...` ----
//   BOARD_S3      = SuperMini S3    (ttyACM0 · USB CDC On Boot: Enabled)
//   BOARD_NODEMCU = NodeMCU clásico (ttyUSB0 · CP2102, sin CDC)
//   BOARD_C3      = ESP32-C3        (ttyACM0 · USB CDC On Boot: Enabled)
#define BOARD_S3       1
#define BOARD_NODEMCU  2
#define BOARD_C3       3

#define BOARD BOARD_C3   // <-- poné acá la placa que vas a flashear

// MIC_DB_OFFSET: corrección de calibración por placa. El periférico I2S de cada
// silicio deja la muestra de 24 bits del INMP441 en distinta posición dentro de
// la palabra de 32 bits, así que el mismo `>> 8` da un nivel distinto por chip.
// Medido en vivo: la C3 (co2-02) y el S3 (co2-01) leen ~-53/-55 dBFS (escala
// correcta: los dos usan el I2S moderno); los ESP32 clásicos (NodeMCU) leen ~-15,
// unos 35-42 dB de más (periférico I2S viejo). Referencia = 0 para C3 y S3; solo
// el NodeMCU se normaliza (~-35 a -42). Afinar contra un celular con app de SPL:
// offset = dB_celular - dB_nodo (mismo lugar/sonido).
#if   BOARD == BOARD_S3
  #define SCD_SDA 8
  #define SCD_SCL 9
  #define I2S_BCK 5    // SCK
  #define I2S_WS  4    // WS
  #define I2S_DIN 6    // SD
  #define MIC_DB_OFFSET -17.0f   // S3 con piso de ruido eléctrico alto (~59 con 0). -17 lo
                                 // clava en ~42, igual que los NodeMCU / el sonómetro.
                                 // Cosmético: queda plano igual, pero al nivel correcto.
  #define TEMP_OFFSET   4.0f      // default del SCD4x; el S3 lee ~como la C3
#elif BOARD == BOARD_NODEMCU
  #define SCD_SDA 21
  #define SCD_SCL 22
  #define I2S_BCK 26   // SCK
  #define I2S_WS  25   // WS
  #define I2S_DIN 33   // SD
  #define MIC_DB_OFFSET -35.0f   // verificar contra celular
  #define TEMP_OFFSET   7.0f      // +3 sobre el default: el NodeMCU calienta ~3C más
#elif BOARD == BOARD_C3
  // Evita strapping (2/8/9), flash (12-17) y USB (18/19) del C3
  #define SCD_SDA 4
  #define SCD_SCL 5
  #define I2S_BCK 6    // SCK
  #define I2S_WS  7    // WS
  #define I2S_DIN 10   // SD
  #define MIC_DB_OFFSET -6.0f    // C3 leía ~48 (6 dB alto vs sonómetro); -6 lo baja a ~42
  #define TEMP_OFFSET   4.0f      // default del SCD4x (la C3 es la más fría, referencia)
#else
  #error "Definí BOARD = BOARD_S3 / BOARD_NODEMCU / BOARD_C3"
#endif

// INMP441 — micrófono (I2S RX)
#define SAMPLE_RATE 16000

#define NODE_TYPE "co2"
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

SensirionI2cScd4x scd4x;
i2s_chan_handle_t rx_chan = NULL;
WiFiClientSecure net;
PubSubClient mqtt(net);
Preferences prefs;

static char errMsg[64];
static float gNoiseDb = -100.0f;
static uint32_t seq = 0;

// Salud del enlace WiFi. rssi = fuerza de señal (dBm, negativo: cerca de 0 es
// mejor), se lee por muestra. wifiReconn = cuantas veces el nodo se reconecto
// desde el boot (enlace inestable -> contador alto). No se persiste: es "desde
// el ultimo arranque".
static uint16_t wifiReconn = 0;
static bool wifiEverConnected = false;

// Suavizado EWMA de las lecturas (el SCD41 tiene ~+-2-3 ppm de ruido propio).
// Alpha 0.3 = promedio de los ultimos ~3-4 samples (~15-20s). Consistente con
// el nodo air.
static const float EWMA_ALPHA = 0.3f;
static float co2Ewma = 0, tempEwma = 0, humEwma = 0, noiseEwma = 0;
static bool ewmaInit = false;

// ---- Buffer store-and-forward (patron tipo Redis RDB: cola en RAM + snapshot
// periodico a flash). La RAM es la cola viva que drena a MQTT; cada 5 min, si
// cambio, se vuelca a LittleFS para sobrevivir un reboot/brownout (se pierde a
// lo sumo la ultima ventana de ~5 min). Las lecturas viajan con su ts original,
// asi que las atrasadas rellenan en el tiempo correcto en InfluxDB. ----
struct Reading {
  uint32_t ts;
  uint32_t seq;
  uint16_t co2;
  int16_t rssi;     // fuerza de señal WiFi al momento de la lectura (dBm)
  uint16_t reconn;  // reconexiones WiFi acumuladas desde el boot
  float temp, hum, noise;
  float noise_l90, noise_l10;  // percentiles de Leq de 1 s (fondo / picos)
};

static const int BUF_CAP = 1440;  // ~12 h a 30 s por muestra (~35 KB de RAM)
// Cambiá este magic si cambia el layout de Reading: invalida cualquier /buf.bin
// viejo para no cargar basura mal alineada tras un cambio de formato.
static const uint32_t BUF_MAGIC = 0x42554633;  // "BUF3"
static Reading ring[BUF_CAP];
static int bufHead = 0, bufCount = 0;
static bool bufDirty = false;

void bufPush(const Reading& r) {
  int tail = (bufHead + bufCount) % BUF_CAP;
  ring[tail] = r;
  if (bufCount < BUF_CAP) bufCount++;
  else bufHead = (bufHead + 1) % BUF_CAP;  // cola llena: pisa la mas vieja
  bufDirty = true;
}

void buildPayload(char* out, size_t n, const Reading& r) {
  snprintf(out, n,
           "{\"type\":\"%s\",\"device\":\"%s\",\"seq\":%u,\"ts\":%u,"
           "\"co2_ppm\":%u,\"temp_c\":%.1f,\"hum_pct\":%.1f,"
           "\"noise_dbfs\":%.1f,\"noise_l90\":%.1f,\"noise_l10\":%.1f,"
           "\"rssi_dbm\":%d,\"wifi_reconn\":%u}",
           NODE_TYPE, DEVICE_ID, r.seq, r.ts, r.co2, r.temp, r.hum,
           r.noise, r.noise_l90, r.noise_l10, r.rssi, r.reconn);
}

// Drena de a poco (NO en rafaga: una rafaga de WiFi puede re-disparar el
// brownout en una fuente marginal). Hasta 2 mensajes cada 500ms = ~4/s.
void bufDrain() {
  static uint32_t lastDrain = 0;
  if (bufCount == 0 || !mqtt.connected() || millis() - lastDrain < 500) return;
  lastDrain = millis();
  for (int k = 0; k < 2 && bufCount > 0 && mqtt.connected(); k++) {
    char payload[320];
    buildPayload(payload, sizeof(payload), ring[bufHead]);
    if (!mqtt.publish(TOPIC, payload)) break;  // sin ACK: reintenta despues
    bufHead = (bufHead + 1) % BUF_CAP;
    bufCount--;
    bufDirty = true;
  }
}

void bufSave() {
  File f = LittleFS.open("/buf.bin", "w");
  if (!f) return;
  f.write((const uint8_t*)&BUF_MAGIC, sizeof(BUF_MAGIC));
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
  uint32_t magic = 0;
  if (f.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic) || magic != BUF_MAGIC) {
    f.close();  // formato viejo/incompatible: se ignora el buffer persistido
    return;
  }
  int cnt = 0;
  if (f.read((uint8_t*)&cnt, sizeof(cnt)) != sizeof(cnt) || cnt < 0 || cnt > BUF_CAP) {
    f.close();
    return;
  }
  bufHead = 0;
  bufCount = 0;
  for (int i = 0; i < cnt; i++) {
    Reading r;
    if (f.read((uint8_t*)&r, sizeof(Reading)) != sizeof(Reading)) break;
    ring[i] = r;
    bufCount++;
    if (r.seq > seq) seq = r.seq;  // el contador nunca por debajo de lo recuperado
  }
  f.close();
  Serial.printf("buffer recuperado de flash: %d lecturas\n", bufCount);
}

const time_t TIME_FLOOR = 1700000000;  // 2023 — por debajo, el reloj no es real


void scd41Init() {
  Wire.begin(SCD_SDA, SCD_SCL);
  scd4x.begin(Wire, SCD41_I2C_ADDR_62);

  delay(500);  // deja que el SCD4x termine su power-up

  // Aseguramos estado idle: si venía midiendo (soft reset), hay que pararlo para
  // poder setear el offset de temperatura (el sensor solo lo acepta quieto).
  scd4x.stopPeriodicMeasurement();
  delay(500);

  uint64_t serial = 0;
  int16_t err = scd4x.getSerialNumber(serial);
  if (err == 0) {
    Serial.print("SCD41 serial: 0x");
    Serial.print((uint32_t)(serial >> 32), HEX);
    Serial.println((uint32_t)(serial & 0xFFFFFFFF), HEX);
  } else {
    Serial.print("SCD41 no responde: ");
    errorToString(err, errMsg, sizeof errMsg);
    Serial.println(errMsg);
  }

  // Offset de temperatura: compensa el autocalentamiento de la placa (el SCD4x
  // está pegado al ESP, que calienta y sesga la temperatura hacia arriba). Se
  // setea en cada arranque (no en EEPROM: no gasto ciclos y se cambia reflasheando).
  // Valor por placa; afinar contra un termómetro de referencia.
  err = scd4x.setTemperatureOffset(TEMP_OFFSET);
  if (err != 0) {
    Serial.print("setTemperatureOffset err: ");
    errorToString(err, errMsg, sizeof errMsg);
    Serial.println(errMsg);
  } else {
    Serial.print("Temp offset = "); Serial.print(TEMP_OFFSET); Serial.println(" C");
  }

  // SCD40: medición periódica (5s). El low-power (30s) es solo-SCD41, no aplica.
  err = scd4x.startPeriodicMeasurement();
  if (err != 0) {
    Serial.print("startPeriodicMeasurement err: ");
    errorToString(err, errMsg, sizeof errMsg);
    Serial.println(errMsg);
  }
}

void i2sInit() {
  i2s_chan_config_t chan_cfg =
    I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_BCK,
      .ws = (gpio_num_t)I2S_WS,
      .dout = I2S_GPIO_UNUSED,
      .din = (gpio_num_t)I2S_DIN,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };
  if (i2s_new_channel(&chan_cfg, NULL, &rx_chan) != ESP_OK) {
    Serial.println("i2s: i2s_new_channel FALLO");
    return;
  }
  if (i2s_channel_init_std_mode(rx_chan, &std_cfg) != ESP_OK) {
    Serial.println("i2s: init_std_mode FALLO");
    return;
  }
  i2s_channel_enable(rx_chan);
  Serial.println("i2s: OK");
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (wifiEverConnected) wifiReconn++;  // caida y reconexion (el primer connect no cuenta)
  Serial.print("WiFi");
#if BOARD == BOARD_C3
  // C3 SuperMini: la radio no asocia a TX máxima. Solo el C3 (no toca S3/NodeMCU).
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
    if (millis() - t0 > 20000) {
      Serial.println(" timeout -> reboot");
      ESP.restart();
    }
  }
  wifiEverConnected = true;
  Serial.print(" ok RSSI=");
  Serial.println(WiFi.RSSI());
}

void syncTime() {
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  Serial.print("NTP");
  uint32_t t0 = millis();
  while (time(nullptr) < TIME_FLOOR) {
    delay(300);
    Serial.print(".");
    if (millis() - t0 > 20000) {
      Serial.println(" timeout -> reboot");
      ESP.restart();
    }
  }
  Serial.print(" ok epoch=");
  Serial.println((long)time(nullptr));
}

void connectMQTT() {
  uint32_t t0 = millis();
  while (!mqtt.connected()) {
    Serial.print("MQTT...");
    if (mqtt.connect(DEVICE_ID, MQTT_USER, MQTT_PASS)) {
      Serial.println(" ok");
      return;
    }
    Serial.print(" rc=");
    Serial.print(mqtt.state());
    if (millis() - t0 > 30000) {
      Serial.println(" timeout -> reboot");
      ESP.restart();
    }
    delay(1500);
  }
}

void setup() {
  Serial.begin(115200);
  // No bloquear el arranque headless: en placas con USB nativo (S3) `Serial`
  // queda en false hasta que una PC abre el puerto. Si arranca de un enchufe 5V
  // sin host, esto colgaba el setup y el nodo nunca publicaba. Esperamos <=2s.
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 2000;) delay(10);

  i2sInit();    // mic (I2S) primero
  scd41Init();  // gas (I2C) despues

  prefs.begin("telemetry", false);
  seq = prefs.getUInt("seq", 0);

  if (!LittleFS.begin(true)) Serial.println("LittleFS no monta (buffer solo en RAM)");
  else bufLoad();

  net.setCACert(ROOT_CA);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(384);  // payload ahora lleva noise_l90/l10

  connectWiFi();
  syncTime();
  connectMQTT();

  Serial.println("listo: " TOPIC);
}

// ---- A-weighting (IEC 61672) sobre el mic, 2 biquads a fs=16 kHz. Aproxima
//      la curva dentro de 0-8 kHz (<0.5 dB hasta 2k, <1 dB hasta 4k); omite el
//      polo de 12.2 kHz, que cae por encima de Nyquist. Coeficientes por
//      bilineal prewarp a 1 kHz (0 dB ahi). Se aplica muestra a muestra antes
//      de acumular energia, asi el Leq sale ponderado.
struct AwBiquad { float b0, b1, b2, a1, a2; };
static const AwBiquad AW1 = { 1.0f, -2.0f, 1.0f, -1.98367651f, 0.983743122f };
static const AwBiquad AW2 = { 1.0f, -2.0f, 1.0f, -1.70207048f, 0.712808195f };
static const float AW_K = 1.05887672f;
static float aw1x1 = 0, aw1x2 = 0, aw1y1 = 0, aw1y2 = 0;
static float aw2x1 = 0, aw2x2 = 0, aw2y1 = 0, aw2y2 = 0;

static inline float awProcess(float x) {
  float y1 = AW1.b0 * x + AW1.b1 * aw1x1 + AW1.b2 * aw1x2 - AW1.a1 * aw1y1 - AW1.a2 * aw1y2;
  aw1x2 = aw1x1; aw1x1 = x; aw1y2 = aw1y1; aw1y1 = y1;
  float y2 = AW2.b0 * y1 + AW2.b1 * aw2x1 + AW2.b2 * aw2x2 - AW2.a1 * aw2y1 - AW2.a2 * aw2y2;
  aw2x2 = aw2x1; aw2x1 = y1; aw2y2 = aw2y1; aw2y1 = y2;
  return AW_K * y2;
}

// ---- L90 / L10: distribucion de Leq de 1 s. Guardamos un Leq de 1 s por
// segundo en un anillo; en cada publish sacamos los percentiles. L90 = nivel
// superado el 90% del tiempo (ruido de fondo); L10 = superado el 10% (picos).
// Con el A-weighting activo son LA90/LA10. Ventana = LSTAT_SECONDS. ----
#define LSTAT_SECONDS 300        // ventana de la estadistica (5 min)
static float    lstatRing[LSTAT_SECONDS];
static int      lstatCount = 0, lstatHead = 0;
static double   lstatSumSq = 0;   // acumulador del Leq de 1 s en curso
static uint32_t lstatRaw = 0;
static uint32_t lstatLast = 0;

static void lstatPush(float leq1) {
  lstatRing[lstatHead] = leq1;
  lstatHead = (lstatHead + 1) % LSTAT_SECONDS;
  if (lstatCount < LSTAT_SECONDS) lstatCount++;
}

// p=90 -> L90 (nivel superado 90% del tiempo = percentil 10 de los Leq cortos)
static float lstatPercentile(float p) {
  if (lstatCount == 0) return -100.0f;
  static float tmp[LSTAT_SECONDS];
  int n = lstatCount;
  memcpy(tmp, lstatRing, n * sizeof(float));
  for (int i = 1; i < n; i++) {          // insertion sort (n <= 300)
    float v = tmp[i];
    int j = i - 1;
    while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; j--; }
    tmp[j + 1] = v;
  }
  int idx = (int)((100.0f - p) / 100.0f * (n - 1));
  if (idx < 0) idx = 0;
  if (idx >= n) idx = n - 1;
  return tmp[idx];
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  // --- ruido (I2S): acumulación CONTINUA de energía para un Leq de la ventana,
  // con A-weighting por muestra. En cada vuelta drenamos lo que haya en el DMA
  // (lectura corta, no bloqueante) y acumulamos suma y suma de cuadrados. ----
  static double   noiseSum = 0;
  static double   noiseSumSq = 0;   // double: la ventana larga desborda int64
  static uint32_t noiseCount = 0;
  {
    int32_t buf[256];
    size_t br = 0;
    if (i2s_channel_read(rx_chan, buf, sizeof(buf), &br, pdMS_TO_TICKS(20)) == ESP_OK) {
      int n = br / sizeof(int32_t);
      for (int i = 0; i < n; i++) {
        float x = awProcess((float)(buf[i] >> 8));  // A-weighting
        noiseSum += x;                // media -> saca el DC remanente
        noiseSumSq += (double)x * x;
        noiseCount++;
        lstatSumSq += (double)x * x;  // insumo del Leq de 1 s (L90/L10)
        lstatRaw++;
      }
    }
  }

  // Cierre del Leq de 1 s: alimenta el anillo de percentiles L90/L10.
  if (millis() - lstatLast >= 1000) {
    lstatLast = millis();
    if (lstatRaw > 0) {
      double msq = lstatSumSq / lstatRaw;
      lstatPush(10.0f * log10f((float)msq / (8388608.0f * 8388608.0f)) + MIC_DB_OFFSET);
    }
    lstatSumSq = 0;
    lstatRaw = 0;
  }

  static uint32_t lastPub = 0;
  if (millis() - lastPub >= 5000) {
    lastPub = millis();

    // Leq de la ventana: RMS con remoción de DC (media), referido a full-scale
    // 2^23 (0 dBFS = seno a fondo de escala). MIC_DB_OFFSET empareja las placas.
    // Se calcula y resetea acá, fuera del read del SCD, para no arrastrar la
    // ventana si el gas no está listo.
    if (noiseCount > 0) {
      double mean = (double)noiseSum / noiseCount;
      double var  = noiseSumSq / noiseCount - mean * mean;
      float  rms  = var > 0 ? sqrtf((float)var) : 0.0f;
      gNoiseDb = 20.0f * log10f((rms + 1.0f) / 8388608.0f) + MIC_DB_OFFSET;
    }
    noiseSum = 0;
    noiseSumSq = 0;
    noiseCount = 0;

    float l90 = lstatPercentile(90.0f);  // fondo
    float l10 = lstatPercentile(10.0f);  // picos

    bool ready = false;
    if (scd4x.getDataReadyStatus(ready) == 0 && ready) {
      uint16_t co2 = 0;
      float t = 0, rh = 0;
      int16_t rerr = scd4x.readMeasurement(co2, t, rh);
      for (int r = 0; r < 3 && rerr != 0; r++) {  // reintento por brownout transitorio
        delay(100);
        rerr = scd4x.readMeasurement(co2, t, rh);
      }
      if (rerr == 0) {
        float co2f = (float)co2;
        co2Ewma = ewmaInit ? (EWMA_ALPHA * co2f + (1.0f - EWMA_ALPHA) * co2Ewma) : co2f;
        tempEwma = ewmaInit ? (EWMA_ALPHA * t + (1.0f - EWMA_ALPHA) * tempEwma) : t;
        humEwma = ewmaInit ? (EWMA_ALPHA * rh + (1.0f - EWMA_ALPHA) * humEwma) : rh;
        noiseEwma = ewmaInit ? (EWMA_ALPHA * gNoiseDb + (1.0f - EWMA_ALPHA) * noiseEwma) : gNoiseDb;
        ewmaInit = true;

        // Encolar (con su ts y seq); el envio real lo hace bufDrain().
        Reading r;
        r.ts = (uint32_t)time(nullptr);
        r.seq = ++seq;
        r.co2 = (uint16_t)lroundf(co2Ewma);
        r.rssi = (int16_t)WiFi.RSSI();  // señal WiFi al momento de la lectura
        r.reconn = wifiReconn;
        r.temp = tempEwma;
        r.hum = humEwma;
        r.noise = noiseEwma;
        r.noise_l90 = l90;
        r.noise_l10 = l10;
        prefs.putUInt("seq", seq);  // seq persistido en cada generacion -> nunca se repite tras reboot
        bufPush(r);
        Serial.printf("CO2=%u(->%u) T=%.1f RH=%.1f noise=%.1f L90=%.1f L10=%.1f RSSI=%d rec=%u enq (cola=%d)\n",
                      co2, r.co2, tempEwma, humEwma, noiseEwma, l90, l10, r.rssi, r.reconn, bufCount);
      }
    }
  }

  // Envio store-and-forward + snapshot periodico a flash (cada 5 min si cambio).
  bufDrain();
  static uint32_t lastSnap = 0;
  if (bufDirty && millis() - lastSnap >= 300000) {
    lastSnap = millis();
    bufSave();
  }
}
