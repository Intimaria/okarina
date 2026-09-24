#!/usr/bin/env python3
"""
Consulta InfluxDB cada PERIOD_S segundos y escribe /data/latest.json con los
últimos valores de cada nodo. La página estática lo lee same-origin como archivo,
así que no hace falta CORS ni exponer la base de datos.
Sin dependencias: solo stdlib (urllib).
"""
import json, os, time, urllib.parse, urllib.request

INFLUX = os.environ.get("INFLUX_URL", "http://influxdb:8086")
DB     = os.environ.get("INFLUX_DB", "iot")
OUT    = os.environ.get("OUT", "/data/latest.json")
PERIOD = float(os.environ.get("PERIOD_S", "10"))
# last(*) trae el último valor de cada campo por serie; agrupamos por nodo y tipo.
Q = 'SELECT last(*) FROM "telemetry" WHERE time > now() - 10m GROUP BY "device","type"'


def fetch():
    url = f"{INFLUX}/query?" + urllib.parse.urlencode({"db": DB, "q": Q})
    with urllib.request.urlopen(url, timeout=10) as r:
        data = json.load(r)
    nodes = {}
    for res in data.get("results", []):
        for s in res.get("series", []):
            tags = s.get("tags", {})
            dev  = tags.get("device", "?")
            cols = s.get("columns", [])
            vals = (s.get("values") or [[]])[0]
            rec = {"type": tags.get("type")}
            for c, v in zip(cols, vals):
                if c == "time":
                    continue
                rec[c[5:] if c.startswith("last_") else c] = v
            nodes[dev] = rec
    return nodes


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    while True:
        try:
            nodes = fetch()
            tmp = OUT + ".tmp"
            with open(tmp, "w") as f:
                json.dump({"generated": int(time.time()), "nodes": nodes}, f)
            os.replace(tmp, OUT)   # escritura atómica: la página nunca lee a medias
        except Exception as e:
            print("error consultando influx:", e, flush=True)
        time.sleep(PERIOD)


if __name__ == "__main__":
    main()
