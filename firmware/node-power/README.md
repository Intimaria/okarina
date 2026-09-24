# Nodo power — SCT-013 + ADS1115 (corriente)

**Estado: andando.** Publica `current_a` en `iot/power/power-01/telemetry`.

Mide la corriente de **un** circuito con una pinza no invasiva **SCT-013**, leída
por un **ADS1115** (I2C, 16-bit). Placa: **SuperMini S3**.

## Hardware / pines

| | |
|---|---|
| Placa | SuperMini S3 (USB CDC On Boot: Enabled) |
| I2C | SDA=`GPIO9`, SCL=`GPIO10` |
| ADS1115 | addr `0x48` (ADDR a GND), canal `A0`, GAIN_ONE (±4.096 V), 860 SPS |
| Circuito | bias/divisor a **1.65 V** + cap 10uF (16V) + 100nF cerámico |

La pinza entrega una señal AC centrada en 0; el bias la sube a ~1.65 V para que el
ADS (single-ended) la lea. El firmware calcula el **RMS de la parte AC** sacando ese
bias por varianza.

## Calibración

`CAL_A_PER_V` en `node-power.ino` (hoy `67.0`), ajustado contra un enchufe medidor
(la pinza sub-leía ~11%). Para recalibrar: poné una carga conocida, medila con una
referencia y corregí el factor.

## Alcance (importante)

- Mide **un circuito** (un conductor), no el total del predio.
- Con la pinza sola **no hay watts**: falta tensión y factor de potencia.
- Para W reales: sensor de tensión (p. ej. ZMPT101B) en el **mismo** nodo.
- La tensión real la aporta el enchufe Tuya: ver `tuya-publisher/`.

## Firmware

`node-power.ino` — TLS + auth + store-and-forward. `DEVICE_ID` único por nodo
(`power-01`). Buffer de ~2 h a 5 s. `secrets.h` gitignored (copiar de
`secrets.example.h`). Sketches viejos/diagnóstico en `recuperados/`.

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
