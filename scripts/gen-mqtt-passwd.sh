#!/usr/bin/env bash
# Genera mosquitto/passwd (hash) a partir de MQTT_USER/MQTT_PASS del entorno
# (cargados por .envrc vía sops). El archivo resultante es gitignored.
set -euo pipefail

: "${MQTT_USER:?set MQTT_USER (via .envrc / sops)}"
: "${MQTT_PASS:?set MQTT_PASS (via .envrc / sops)}"

docker run --rm -i eclipse-mosquitto:2.0 sh -c \
  "mosquitto_passwd -c -b /tmp/passwd '${MQTT_USER}' '${MQTT_PASS}' >/dev/null 2>&1 && cat /tmp/passwd" \
  > mosquitto/passwd

echo "mosquitto/passwd generado para el usuario '${MQTT_USER}'"
