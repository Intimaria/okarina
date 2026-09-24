# mTLS para la telemetría IoT — spec y plan

> Mejora futura, no implementada. Endurece la autenticación de los nodos ESP32
> contra el broker MQTT agregando certificado de cliente (TLS mutuo) además de
> la password sobre TLS que ya existe. Requiere reflashear todos los nodos, así
> que se deja documentada para hacerla fuera del evento.
>
> **Decisión: no se implementa en este evento; queda como plan a futuro.**

## 1. Objetivo

Que el broker pueda **rechazar en el handshake TLS** a cualquier cliente que no
presente un certificado firmado por nuestra CA, antes de llegar a la capa MQTT.
Hoy el puerto 443 está abierto a internet: los bots que lo escanean conectan,
no hablan MQTT y caen (`protocol error`), pero la única barrera real de
identidad es la password. Con mTLS, el que no tiene certificado ni siquiera
completa el túnel.

## 2. Estado actual (lo que ya hay)

Autenticación en el orden en que ocurre:

1. El nodo abre TCP contra `<broker-host>:443`.
2. El broker presenta su **certificado de servidor** (Let's Encrypt, emitido por
   certbot en `ansible/roles/certbot/`).
3. El nodo lo valida contra la CA pineada en el firmware (**ISRG Root X1**, el
   string `ROOT_CA` en `firmware/node-co2/node-co2.ino` y `node-air/node-air.ino`,
   aplicado con `net.setCACert(ROOT_CA)`).
4. Queda el túnel cifrado. Recién adentro, el nodo manda MQTT CONNECT con
   **usuario + password** (`nodos` / password, en `secrets.h`).

Config del broker: `mosquitto/mosquitto.conf` — `allow_anonymous false`,
`password_file`, listener 443 con `certfile`/`keyfile` (cert de servidor). **No**
hay `cafile` ni `require_certificate`, así que no se pide cert de cliente.

Barrera de identidad actual: **la password**. Es aceptable con TLS + fail2ban,
pero es un único secreto compartido por todos los nodos.

## 3. Qué cambia con mTLS

En el handshake, después del paso 2, el broker agrega un **CertificateRequest**:
le pide un certificado al nodo. El nodo presenta **su propio certificado de
cliente** y prueba que tiene la clave privada asociada. El broker verifica que
ese certificado esté firmado por **nuestra CA de clientes**. Si no lo está (o no
hay certificado), **aborta el handshake** — antes de MQTT, antes de la password.

La identidad pasa de "adentro del túnel, por password" a "durante el handshake,
por certificado".

## 4. Componentes a crear (la PKI)

### 4.1 CA de clientes (propia, separada de Let's Encrypt)

Ancla de confianza de las identidades de los nodos. La clave privada de la CA
**no vive en el broker**: se guarda cifrada con SOPS/AGE en el repo y se usa
localmente para firmar. Al broker solo va el certificado público de la CA.

```
openssl genrsa -out client-ca.key 4096
openssl req -x509 -new -key client-ca.key -days 1825 \
  -out client-ca.pem -subj "/CN=evento-iot-client-ca"
```

### 4.2 Un certificado por nodo

Uno por nodo (revocable individualmente), con el `DEVICE_ID` como CN:

```
openssl genrsa -out co2-01.key 2048
openssl req -new -key co2-01.key -out co2-01.csr -subj "/CN=co2-01"
openssl x509 -req -in co2-01.csr -CA client-ca.pem -CAkey client-ca.key \
  -days 730 -out co2-01.crt
```

Emitir también un certificado para el **tooling propio** (`mosquitto_sub` de
prueba, monitoreo), porque con `require_certificate true` también se lo pide.

## 5. Cambios en el broker

En `mosquitto/mosquitto.conf`, sobre el listener 443, agregar:

```
cafile /mosquitto/config/certs/client-ca.pem
require_certificate true
# opcional: usar el CN del cert como usuario MQTT y soltar la password
# use_identity_as_username true
```

Publicar `client-ca.pem` en el volumen de certs del contenedor (junto a
`fullchain.pem`/`privkey.pem`) vía el rol `ansible/roles/stack`. La clave de la
CA **no** se copia al servidor.

## 6. Cambios en el firmware

En cada nodo (`node-co2.ino`, `node-air.ino`), sobre `WiFiClientSecure net;`,
además del `setCACert` que ya está:

```c
net.setCACert(ROOT_CA);          // ya existe: valida al server
net.setCertificate(CLIENT_CERT); // nuevo: cert del nodo
net.setPrivateKey(CLIENT_KEY);   // nuevo: clave privada del nodo
```

`CLIENT_CERT` y `CLIENT_KEY` van embebidos como strings en `secrets.h` (cifrado
con SOPS), **distintos por nodo**. La librería los presenta sola cuando el broker
los pide. Costo de flash: unos pocos KB por nodo, despreciable.

## 7. Secretos y rotación

- La **clave de la CA** vive cifrada (SOPS/AGE) en el repo, nunca en el broker.
- Las **claves de nodo** viven en `secrets.h` cifrado, una por nodo.
- **Validez**: certificados de nodo con vencimiento cómodo (ej. 2 años); CA más
  larga (5 años). Vencer = reflashear.
- **Revocación**: en un Mosquitto chico no hay OCSP/CRL práctico. Revocar un
  nodo comprometido implica re-emitir/rotar y reflashear. Es tosco: asumirlo.

## 8. Plan de implementación (rollout sin cortar el fleet)

Poner `require_certificate true` en el listener 443 existente **tumba de golpe**
todos los nodos actuales (no tienen cert) hasta reflashearlos. Para migrar sin
downtime, aprovechando que reflashear es necesario de todos modos:

1. Generar CA de clientes y certificados (nodos + tooling). Guardar todo cifrado.
2. Agregar en `mosquitto.conf` un **listener nuevo** (ej. 8884) con
   `cafile` + `require_certificate true`. Dejar el 443 (password) vivo.
3. Publicar `client-ca.pem` al broker y recargar Mosquitto.
4. Reflashear **un** nodo apuntándolo al puerto nuevo + con su cert/clave.
   Verificar en el broker que publica (suscripción interna). Repetir nodo a nodo.
5. Cuando **todos** los nodos migraron y se verificaron, retirar el listener 443
   password-only (o agregarle `require_certificate` una vez que nada use password).
6. Actualizar el tooling de prueba/monitoreo para presentar su cert.

## 9. Trade-offs y riesgos (honestos)

- **Clave privada en el flash**: quien lea el flash del ESP32 extrae la clave del
  nodo. Cert por nodo limita el daño (revocás uno), pero no lo elimina. Es la
  debilidad de fondo de mTLS en dispositivos.
- **Rotar = reflashear**: cada vencimiento o filtración obliga a reflashear.
- **Tu tooling también necesita cert** una vez activado `require_certificate`.
- **Revocación tosca** (sin CRL/OCSP): en la práctica, re-emitir + reflashear.
- **Ganancia marginal para un evento temporal**: con TLS + password + fail2ban ya
  hay seguridad razonable. mTLS es lo correcto para un fleet **permanente**; para
  un evento de fin de semana es mucha maquinaria de PKI para poco extra.

## 10. Criterios de "listo"

- CA de clientes creada y guardada cifrada (clave nunca en el broker).
- Un certificado por nodo + uno para el tooling, emitidos y cifrados en `secrets.h`.
- Listener mTLS activo con `require_certificate true`; handshake rechaza a un
  cliente sin cert (probado con `mosquitto_sub` sin cert → falla en TLS).
- Todos los nodos migrados y publicando por el listener nuevo.
- Listener password-only retirado (o endurecido).
