# Nodo co2 (calibrado) — SCD4x + INMP441

**Estado: es el firmware que corre en la flota.** Publica `type=co2` por MQTT/TLS al broker.

CO₂, temperatura y humedad con un **SCD4x** (SCD40/SCD41, intercambiables) y nivel de ruido
con un micrófono **INMP441** (I2S). `node-co2.ino` es la variante vieja ("solo offset"),
se conserva como referencia; esta agrega **Leq + A-weighting** y compensación de temperatura.

## Placas y pines

Se elige con `#define BOARD ...` (una sola línea):

| BOARD | Placa | I2C SDA/SCL | I2S SCK/WS/SD | `MIC_DB_OFFSET` | `TEMP_OFFSET` |
|---|---|---|---|---|---|
| `BOARD_S3` | SuperMini S3 | 8 / 9 | 5 / 4 / 6 | -17 | 4.0 |
| `BOARD_NODEMCU` | NodeMCU (ESP32 clásico) | 21 / 22 | 26 / 25 / 33 | -35 | 7.0 |
| `BOARD_C3` | ESP32-C3 | 4 / 5 | 6 / 7 / 10 | -6 | 4.0 |

- `MIC_DB_OFFSET`: el periférico I2S de cada silicio deja la muestra de 24 bits del INMP441
  en distinta posición dentro de la palabra de 32 bits, así que el mismo `>> 8` da un nivel
  distinto por chip. Se afina contra un celular con app de SPL.
- `TEMP_OFFSET`: compensa el autocalentamiento de la placa (el SCD4x calienta y sesga la
  temperatura hacia arriba). Se setea en cada arranque.

## Qué publica

Topic `iot/co2/<DEVICE_ID>/telemetry`, cada 5 s:

```json
{"type":"co2","device":"co2-01","seq":1234,"ts":1756000000,
 "co2_ppm":654,"temp_c":19.3,"hum_pct":59.3,"noise_dbfs":-42.1,
 "noise_l90":-45.0,"noise_l10":-38.5,"rssi_dbm":-67,"wifi_reconn":0}
```

- `noise_dbfs`: Leq de la ventana (con A-weighting), suavizado con EWMA.
- `noise_l90` / `noise_l10`: percentiles de Leq de 1 s (fondo / picos) sobre 5 min.
- `rssi_dbm` / `wifi_reconn`: salud del enlace WiFi (señal y reconexiones desde el boot).

## Store-and-forward

Cola en RAM (hasta ~12 h a 30 s) con snapshot periódico a LittleFS. Las lecturas viajan con
su `ts` original, así que las atrasadas rellenan en el tiempo correcto en InfluxDB. El drenaje
a MQTT es de a poco (no en ráfaga) para no disparar brownouts en fuentes marginales.

## Cableado

Ver [`wiring.svg`](wiring.svg) (y `wiring.png`).

## Certificado del broker (CA)

El firmware trae embebida la raíz **ISRG Root X1** de Let's Encrypt (`ROOT_CA`) para validar
al broker. Es el CA **público** —no el certificado del broker ni ningún dato propio— y se
obtiene con:

```bash
curl -s https://letsencrypt.org/certs/isrgrootx1.pem
# o del propio broker:
openssl s_client -connect <broker-host>:443 -showcerts </dev/null
```

Si el broker cambia de dominio o de CA, hay que reflashear con el `ROOT_CA` nuevo.
El host del broker, `MQTT_USER`/`MQTT_PASS` y el `DEVICE_ID` van en `secrets.h` (gitignored,
copiar de `secrets.example.h`).
