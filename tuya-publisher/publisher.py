# /// script
# requires-python = ">=3.9"
# dependencies = ["tinytuya>=1.13", "paho-mqtt>=2.0"]
# ///
"""
Publisher Tuya -> MQTT para el enchufe medidor (BAW TPSWIFI-101 / cualquier Smart Life).

Lee V/A/W del enchufe por LAN (protocolo local, SIN nube) con tinytuya y los
publica al broker del evento con el MISMO esquema que los nodos ESP32:
  topic:   iot/<type>/<device>/telemetry   (JSON)
  status:  iot/<type>/<device>/status      (online/offline, LWT retenido)
Telegraf ya está suscrito a iot/+/+/telemetry, así que entra solo a InfluxDB.

Uso:
  uv run publisher.py --discover   # imprime los DPs del enchufe (para mapear V/A/W)
  uv run publisher.py              # loop: publica cada PERIOD_S segundos

Config por entorno (direnv/SOPS): ver .envrc.example.
"""
import json, os, ssl, sys, time
import tinytuya
import paho.mqtt.client as mqtt


def env(k, d=None):
    return os.environ.get(k, d)


DEV_ID  = env("TUYA_DEVICE_ID")
DEV_IP  = env("TUYA_DEVICE_IP", "Auto")
DEV_KEY = env("TUYA_LOCAL_KEY")
DEV_VER = float(env("TUYA_VERSION", "3.3"))

MQTT_HOST = env("MQTT_HOST", "localhost")
MQTT_PORT = int(env("MQTT_PORT", "1883"))
MQTT_USER = env("MQTT_USER")
MQTT_PASS = env("MQTT_PASS")
MQTT_TLS  = env("MQTT_TLS", "1") == "1"

DEVICE  = env("DEVICE", "plug-01")
NTYPE   = env("NODE_TYPE", "volt")
TOPIC   = f"iot/{NTYPE}/{DEVICE}/telemetry"
STATUS  = f"iot/{NTYPE}/{DEVICE}/status"
PERIOD  = float(env("PERIOD_S", "10"))

# Mapeo de DPs (numero de DP : escala). Confirmar con --discover.
# Defaults típicos Tuya/BAW: V=DP20 (x10), A=DP18 (mA), W=DP19 (x10).
DP_V = env("DP_V", "20"); SC_V = float(env("SC_V", "10"))
DP_A = env("DP_A", "18"); SC_A = float(env("SC_A", "1000"))
DP_W = env("DP_W", "19"); SC_W = float(env("SC_W", "10"))

def make_dev():
    d = tinytuya.OutletDevice(DEV_ID, DEV_IP, DEV_KEY)
    d.set_version(DEV_VER)
    d.set_socketPersistent(True)
    return d


def discover():
    d = make_dev()
    st = d.status()
    print(json.dumps(st, indent=2, ensure_ascii=False))
    dps = st.get("dps", {})
    print("\nDPs detectados:")
    for k, v in sorted(dps.items(), key=lambda x: int(x[0])):
        print(f"  DP{k} = {v}")
    print("\nCruzá estos valores con el display de la app (o un voltímetro) para")
    print("mapear cuál es V/A/W y con qué escala. Ajustá DP_V/DP_A/DP_W en .envrc.")


def make_mqtt():
    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"tuya-{DEVICE}")
    if MQTT_USER:
        c.username_pw_set(MQTT_USER, MQTT_PASS)
    if MQTT_TLS:
        c.tls_set(cert_reqs=ssl.CERT_REQUIRED)  # usa el trust store del sistema (Let's Encrypt)
    c.will_set(STATUS, "offline", retain=True)
    c.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
    c.loop_start()
    c.publish(STATUS, "online", retain=True)
    return c


def mqtt_connect_forever():
    """make_mqtt() con reintento: no morir si el broker no responde al arranque."""
    while True:
        try:
            return make_mqtt()
        except Exception as e:
            print(f"MQTT connect falló: {e!r} -> reintento en 5s", flush=True)
            time.sleep(5)


def read_val(dps, dp, scale):
    if dp in dps and dps[dp] is not None:
        try:
            return round(int(dps[dp]) / scale, 2)
        except (ValueError, TypeError):
            return None
    return None


def main():
    if "--discover" in sys.argv:
        discover()
        return
    for req in ("TUYA_DEVICE_ID", "TUYA_LOCAL_KEY"):
        if not env(req):
            sys.exit(f"Falta {req} (ver .envrc.example / correr el wizard)")

    m = mqtt_connect_forever()
    d = make_dev()
    seq = 0
    while True:
        try:
            if not m.is_connected():                      # watchdog MQTT
                print("MQTT caído -> reconecto", flush=True)
                try:
                    m.reconnect()
                except Exception:
                    m = mqtt_connect_forever()
            dps = d.status().get("dps", {})
            payload = {
                "type": NTYPE, "device": DEVICE, "seq": seq, "ts": int(time.time()),
                "voltage_v": read_val(dps, DP_V, SC_V),   # tensión del enchufe (V)
                "current_a": read_val(dps, DP_A, SC_A),   # corriente del propio enchufe
                "power_w":   read_val(dps, DP_W, SC_W),   # potencia real del propio enchufe
            }
            m.publish(TOPIC, json.dumps(payload))
            print("pub:", payload, flush=True)
            seq += 1
        except Exception as e:
            print("error:", e, "-> reconecto tinytuya", flush=True)
            time.sleep(2)
            try:
                d = make_dev()
            except Exception as e2:
                print("make_dev falló:", e2, flush=True)
        time.sleep(PERIOD)


if __name__ == "__main__":
    main()
