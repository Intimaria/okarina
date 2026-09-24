# Nodo air — MQ135 + DHT22 (para poner a prueba el paper CONICET)

**Estado: firmware listo y andando.** Publica `type=air` por MQTT/TLS al broker.

## Hardware

- Placa: **ESP32-C3** (nodo nuevo). El firmware fija TX a 8.5 dBm y modo STA, que es lo que
  hace asociar a la radio del C3.
- **MQ135** (breakout azul, RL = 1kΩ / SMD "102") → gases, salida analógica.
- **DHT22** (sensor pelado de 4 patas) → temperatura y humedad.

## Cableado

| Desde (ESP32) | A | Notas |
|---|---|---|
| `5V` (VIN) | MQ135 `VCC` | el heater chupa ~150 mA; **NUNCA a 3.3V** (recalienta el AMS1117) |
| `GND` | MQ135 `GND` | masa común |
| MQ135 `AO` → 10k → **nodo** → 10k → `GND` | nodo → `GPIO3` | divisor 2:1 + **100 nF** del nodo a GND |
| MQ135 `DO` | — | sin usar |
| `3V3` | DHT22 pata 1 `VDD` | **3.3V, no 5V** (el pull-up llevaría DATA a 5V y daña el GPIO) |
| `GPIO10` | DHT22 pata 2 `DATA` | + **pull-up 10k** de DATA a `3V3` |
| — | DHT22 pata 3 | NC |
| `GND` | DHT22 pata 4 `GND` | masa común |

DHT22 con la rejilla al frente, patas hacia abajo: **1=VDD, 2=DATA, 3=NC, 4=GND** (izq→der).
Ojo: invertirlo da `nan` (o lo cocina). Fue el error real de la primera vuelta.

Pines en el firmware: `MQ135_PIN 3` (ADC1, input-only), `DHTPIN 10`.

## Gotchas eléctricos

- **El pin del ADC no es 5V-tolerante** y el `AO` del MQ135 llega a ~5V → el divisor 2:1 es **obligatorio**.
- **Es un pin ADC1**: correcto, porque los pines ADC2 dejan de leer cuando prende el WiFi.
- El MQ135 **no es selectivo**: las 5 "curvas" de gas salen del mismo Rs; son un display relativo, no CO2 real.
- `calibrateR0()` corre una vez al boot asumiendo aire limpio (~400 ppm). Encender el nodo **en aire fresco**.

## Firmware

- `node-air.ino` (WiFi + MQTT/TLS 443 + auth `nodos` + CA ISRG Root X1 + schema nuevo).
- Copiar `secrets.example.h` → `secrets.h` (gitignored) y completar WiFi/MQTT + `DEVICE_ID` (**`air-01`**, no `co2-01`).
- Publica a `iot/air/<device>/telemetry`, cada 10 s:
  `{type, device, seq, ts, air_raw, co2_ppm, co_ppm, alcohol_ppm, nh3_ppm, toluene_ppm, temp_c, hum_pct}`.
- Multi-gas: el MQ135 **no distingue gases** — las 5 curvas salen del mismo Rs y se
  mueven juntas. Cada una usa su recta log-log del datasheet (coeficientes del TP2),
  encerada en aire limpio al calibrar, y suavizada con EWMA. Es display rico, no
  medición real de 5 gases.
- Flashear con los sensores **desconectados** (heater 150 mA + flasheo → brownout).

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
