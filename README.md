# evento-backend

Plataforma de telemetría para el evento: nodos ESP32 → MQTT/TLS → Telegraf → InfluxDB → Grafana.

- **MQTT** sobre **443 con TLS + auth** (el 443 es el que el predio deja salir).
- **Grafana** expuesto por **Cloudflare Tunnel** (atado a `localhost:3000`, sin puerto abierto).
- **InfluxDB 1.8** interno (sin puerto al host).
- Provisionamiento con **Ansible**; secretos con **sops/AGE**.

Diseño: [`docs/diseno.md`](docs/diseno.md).

> Los hostnames reales (broker y Grafana), el email del certificado y las contraseñas
> viven cifrados en `secrets.enc.yaml` (sops), no en este repo.

## Requisitos (control node = tu laptop)

- `direnv` + `asdf` (`.tool-versions` pinea `sops` y `age`)
- `ansible` + las colecciones:
  ```bash
  ansible-galaxy collection install -r ansible/requirements.yml
  ```

## Setup inicial (una vez)

1. **Secretos** — crear y cifrar `secrets.enc.yaml`:
   ```bash
   cp secrets.example.yaml secrets.plain.yaml
   # editar MQTT_USER, MQTT_PASS, GRAFANA_ADMIN_PASSWORD, CF_TUNNEL_TOKEN,
   #        mosquitto_domain (hostname del broker) y cert_email
   sops --encrypt --age <recipiente> secrets.plain.yaml > secrets.enc.yaml
   rm secrets.plain.yaml
   ```

2. **Cloudflare Tunnel** — crear el túnel (token) apuntando el hostname de Grafana → `http://grafana:3000`:
   ```bash
   cloudflared tunnel login
   cloudflared tunnel create evento-grafana
   cloudflared tunnel route dns evento-grafana <grafana-host>
   cloudflared tunnel token evento-grafana    # → este token va al CF_TUNNEL_TOKEN de sops
   ```

3. **Inventario** — ajustar `ansible/inventory/hosts.yml` (host + key real).

## Desplegar

```bash
cd ansible
ansible-playbook -i inventory/hosts.yml playbook.yml
```

El playbook: instala Docker + fail2ban + certbot, endurece SSH, sincroniza el repo a
`{{ deploy_dir }}`, templa el `.env`, genera el passwd de MQTT, obtiene el certificado
(HTTP-01 en el puerto 80; dominio y email salen de `secrets.enc.yaml`) y levanta el stack.

## Verificar

```bash
# TLS del broker (usá el valor real de mosquitto_domain)
echo | openssl s_client -connect <broker-host>:443 -servername <broker-host> 2>/dev/null | openssl x509 -noout -subject -issuer

# logs del stack
ssh -i ~/.ssh/id_rsa ubuntu@<server> 'docker compose -f /opt/evento-backend/docker-compose.yml ps'

# grafana por el túnel
open https://<grafana-host>
```

## Estructura

```
ansible/               playbook + roles (hardening, docker, certbot, stack)
docker-compose.yml     stack
mosquitto/             broker (config + passwd + certs)
telegraf/              ingesta MQTT -> InfluxDB (+ placeholder API WiFi)
grafana/provisioning/  datasource + dashboards
scripts/               gen-mqtt-passwd.sh (uso local)
firmware/              firmware de los nodos
tuya-publisher/        lectura del enchufe medidor Tuya -> MQTT (tensión)
secrets.enc.yaml       secretos cifrados (sops)
```

## Nodos

Cada nodo publica a `iot/<type>/<device>/telemetry`:

```json
{"type":"co2","device":"co2-01","seq":1234,"ts":1756000000,
 "co2_ppm":654,"temp_c":19.3,"hum_pct":59.3,"noise_dbfs":-48.3,
 "rssi_dbm":-67,"wifi_reconn":0}
```

El nodo conecta por **MQTT sobre TLS (puerto 443)** con **auth user/pass** y valida el
broker contra la raíz **ISRG Root X1** (Let's Encrypt) embebida en el firmware.

Firmwares en `firmware/`:

- `node-co2-calib` — CO₂, temperatura, humedad y ruido (el que corre en la flota). Agrega
  `noise_l90` / `noise_l10` (Leq + A-weighting) y `TEMP_OFFSET` por placa.
- `node-air` — MQ135 + DHT22 (experimento multi-gas).
- `node-power` — SCT-013 + ADS1115 (corriente).

Cada nodo lleva en su `secrets.h` (gitignored, copiar de `secrets.example.h`) el SSID/pass
WiFi, el host del broker, `MQTT_USER`/`MQTT_PASS` y su `DEVICE_ID`.
