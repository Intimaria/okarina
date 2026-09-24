# Tuya → MQTT (voltaje del enchufe medidor)

Publica V/A/W del enchufe inteligente (BAW TPSWIFI-101 / Smart Life) al broker del
evento, leyéndolo por LAN con tinytuya (sin depender de la nube). El publisher hace
una sola cosa: leer el enchufe y publicarlo. La complementación con la pinza
(**VA = V(enchufe) × I(pinza)**, aparente del circuito) se calcula **en Grafana**, no acá.

Corre en una compu de la LAN del enchufe (no en el ESP). El dato va al broker por
TLS:443, mismo esquema que los nodos: `iot/volt/plug-01/telemetry`.

## 1. Sacar la local key (una vez)

1. Emparejá el enchufe en la app **Smart Life**.
2. Cuenta free en [iot.tuya.com](https://iot.tuya.com) → Cloud project (habilitá las
   APIs *IoT Core* + *Smart Home Basic*) → **Link Tuya App Account** (QR desde la app).
3. Corré el wizard (pide API Key/Secret + región):

   ```bash
   uv run --with tinytuya python -m tinytuya wizard
   ```

   Baja `devices.json` con el **device id**, la **local key** y la IP del enchufe.

## 2. Configurar

```bash
cp .envrc.example .envrc     # completá device id, local key, pass MQTT
direnv allow
```

La local key y el pass MQTT: mejor en SOPS, no en texto plano.

## 3. Descubrir el mapeo de DPs

El enchufe expone sus lecturas como **DPs numerados**. Descubrí cuál es cuál:

```bash
uv run publisher.py --discover
```

Cruzá los valores con el display de la app (o un voltímetro) y ajustá
`DP_V/DP_A/DP_W` y sus escalas en `.envrc`. Típico Tuya: DP20=V×10, DP18=A(mA),
DP19=W×10 — pero confirmalo en TU enchufe.

## 4. Correr

Con `run.sh` (lee `.envrc` y se auto-reinicia si el publisher se cae):

```bash
./run.sh                                   # foreground (Ctrl-C para salir)
setsid nohup ./run.sh > run.log 2>&1 &     # background (sobrevive a cerrar la terminal)
```

Publica cada `PERIOD_S` segundos. Verificá en InfluxDB que `type=volt` (campo `voltage_v`) entre fresco.

## En el evento (runbook)

Datos ya confirmados de ESTE enchufe (BAW): device id `<device-id>`,
región `us`, protocolo **3.4**, mapeo `DP20=V/10, DP18=A(mA), DP19=W/10`.
El publisher corre en una compu conectada a la MISMA red 2.4 GHz que el enchufe.

1. **Nada que emparejar** ✅ La WiFi del evento tiene la MISMA SSID y clave que en
   casa, así que el enchufe (y el nodo corriente) se conectan solos al llegar. La
   local key NO cambia: sigue siendo la de tu `.envrc` (no la pongas acá). Lo único
   que puede cambiar es la IP (paso 2).

   > Fallback: si ALGUNA vez tenés que re-emparejar el enchufe (cambiar de red de
   > verdad), eso SÍ cambia la local key y hay que re-extraerla (necesita internet
   > + las claves del Cloud project):
   >
   > ```bash
   > export TUYA_K="<Access ID>"; export TUYA_S="<Access Secret>"
   > uv run --with tinytuya python - <<'PY'
   > import os, tinytuya
   > c = tinytuya.Cloud(apiRegion="us", apiKey=os.environ["TUYA_K"], apiSecret=os.environ["TUYA_S"])
   > for d in c.getdevices():
   >     if "BAW" in (d.get("name") or ""): print("id:", d["id"], " key:", d["key"])
   > PY
   > ```

2. **Confirmar la IP del enchufe** (el DHCP del lugar puede darle otra). El "Auto"
   a veces no llega por bloqueo de broadcast; los Tuya escuchan en `6668/tcp`:

   ```bash
   sudo nmap -p 6668 --open 192.168.X.0/24   # ajustá la subred del lugar
   ```

   Poné esa IP en `TUYA_DEVICE_IP` (probá cada IP con `--discover` si hay más de un Tuya).

3. **Probar la lectura**: `uv run publisher.py --discover` → deberías ver los DPs y
   `DP20 ≈ 2200` (220.0 V). Si da "Check device key or version", revisá key/IP/versión.

4. **Correr** (portable, se auto-reinicia):

   ```bash
   ./run.sh                                            # foreground
   setsid nohup ./run.sh > run.log 2>&1 &              # background
   ```

5. **Verificar** en InfluxDB que entre fresco:
   `SELECT last(voltage_v) FROM telemetry WHERE type='volt'`.

## Notas

- Una instancia por enchufe (distinto `DEVICE`/`NODE_TYPE`) si sumás más fases.
- Si tinytuya no conecta: probá `TUYA_VERSION` 3.4 o 3.5, y fijá `TUYA_DEVICE_IP`
  a la IP real del enchufe (reservala en el router del lugar si podés).
- Tip para evitar el re-emparejamiento: llevá un hotspot con SSID/clave FIJAS y
  emparejá el enchufe a ese; así la red no cambia entre tu casa y el evento y la
  local key se mantiene.
