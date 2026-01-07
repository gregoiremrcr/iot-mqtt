# MQTT Topics Contract

## Prefix
- `classroom/<deviceId>/...`

## Topics

### `classroom/<deviceId>/telemetry` (device → broker)
Mesures météo publiées périodiquement par les ESP32.

**Payload (JSON) — exemple :**
```json
{
  "temperature": 21.7,
  "humidity": 48.2,
  "battery": 3.91,
  "timestamp": 1736240000000
}
```
- `temperature` : °C
- `humidity` : %
- `battery` : volts (ou % selon firmware)
- `timestamp` : epoch ms (optionnel : sinon on utilise la date de réception)

### `classroom/<deviceId>/events` (device → broker)
Événements ponctuels (ex: reboot, alerte batterie, etc.).

### `classroom/<deviceId>/status` (device → broker)
État (online, fw version, RSSI, etc.). Peut aussi être publié en retained.

### `classroom/<deviceId>/cmd` (broker → device)
Commandes (ex: changer période, reset, LED, etc.).

---

## Bonus Flipper
- `flipper/<deviceId>/<thing>`
Exemples : boutons, capteurs, actions… Le format exact doit être observé via MQTT Explorer.
