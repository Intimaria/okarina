#!/usr/bin/env bash
# Corre el publisher Tuya -> MQTT con auto-reinicio. Portable: sirve igual en
# casa que en el evento. Toda la config (IP del enchufe, credenciales) sale de
# .envrc — en el evento solo cambiás TUYA_DEVICE_IP ahí y reiniciás.
#
#   ./run.sh            # foreground (Ctrl-C para salir)
#   setsid nohup ./run.sh > run.log 2>&1 &   # background
set -u
cd "$(dirname "$0")"

if [ ! -f .envrc ]; then
  echo "[run] falta .envrc (copiá de .envrc.example y completá)" >&2
  exit 1
fi

set -a; . ./.envrc; set +a

while true; do
  echo "[run] $(date -Is) arrancando publisher (plug=${TUYA_DEVICE_IP:-?})"
  uv run publisher.py
  echo "[run] $(date -Is) el publisher salió ($?); reintento en 5 s"
  sleep 5
done
