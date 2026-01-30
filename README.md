# TP IoT — Svelte + MQTT (via Bridge WS)

Ce dépôt contient :
- `bridge/` : un bridge Node.js qui lit MQTT et retransmet en WebSocket
- `front/` : une app SvelteKit qui affiche la météo de 5 stations + un panneau Flipper
- `contracts/` : description des topics + exemples de payload

## Prérequis
- Node.js 18+

## 1) Lancer le bridge
```bash
cd bridge
npm install
npm run dev
```
Le bridge expose un WebSocket sur `ws://localhost:8080`.

## 2) Lancer le front
```bash
cd front
npm install
npm run dev -- --open
```

## Variables utiles
- `VITE_BRIDGE_WS_URL` (front) : URL WS (par défaut `ws://localhost:8080`)
- `MQTT_URL` (bridge) : URL MQTT (par défaut `mqtt://captain.dev0.pandor.cloud:1884`)
- `MQTT_TOPICS` (bridge) : topics séparés par des virgules
  - par défaut : `classroom/+/telemetry,flipper/+/+`

## Notes
- Online/offline : une station est **online** si elle a publié il y a moins de 60s.
- Le parsing du `deviceId` se fait via le topic (`classroom/<deviceId>/telemetry` ou `flipper/<deviceId>/...`).

## Réalisé par

- Grégoire MERCIER
- Alexis HU

