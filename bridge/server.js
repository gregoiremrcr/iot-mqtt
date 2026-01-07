import mqtt from "mqtt";
import { WebSocketServer } from "ws";

const MQTT_URL = process.env.MQTT_URL || "mqtt://captain.dev0.pandor.cloud:1884";
const MQTT_TOPICS = (process.env.MQTT_TOPICS || "classroom/+/telemetry,flipper/+/+")
  .split(",")
  .map((s) => s.trim())
  .filter(Boolean);

const WS_PORT = Number(process.env.WS_PORT || 8080);

function safeJsonParse(buf) {
  const str = buf?.toString?.("utf-8") ?? String(buf);
  try {
    return { ok: true, value: JSON.parse(str) };
  } catch {
    return { ok: false, value: str };
  }
}

function classify(topic) {
  if (topic.startsWith("classroom/")) return "telemetry";
  if (topic.startsWith("flipper/")) return "flipper";
  return "unknown";
}

const wss = new WebSocketServer({ port: WS_PORT });
const clients = new Set();

wss.on("connection", (ws) => {
  clients.add(ws);
  ws.send(JSON.stringify({ type: "hello", ts: Date.now(), topics: MQTT_TOPICS }));
  ws.on("close", () => clients.delete(ws));
});

function broadcast(obj) {
  const msg = JSON.stringify(obj);
  for (const ws of clients) {
    if (ws.readyState === ws.OPEN) ws.send(msg);
  }
}

console.log(`[ws] listening on ws://localhost:${WS_PORT}`);

const mqttClient = mqtt.connect(MQTT_URL, {
  reconnectPeriod: 1000,
  connectTimeout: 10_000,
});

mqttClient.on("connect", () => {
  console.log(`[mqtt] connected to ${MQTT_URL}`);
  mqttClient.subscribe(MQTT_TOPICS, (err) => {
    if (err) console.error("[mqtt] subscribe error:", err);
    else console.log("[mqtt] subscribed:", MQTT_TOPICS.join(", "));
  });
});

mqttClient.on("reconnect", () => console.log("[mqtt] reconnecting..."));
mqttClient.on("error", (e) => console.error("[mqtt] error:", e?.message || e));
mqttClient.on("close", () => console.log("[mqtt] connection closed"));

mqttClient.on("message", (topic, payload) => {
  const parsed = safeJsonParse(payload);
  const out = {
    type: "mqtt",
    kind: classify(topic),
    topic,
    payload: parsed.value,
    payloadIsJson: parsed.ok,
    ts: Date.now(),
  };
  // log léger
  if (out.kind === "telemetry") {
    const deviceId = topic.split("/")[1] || "?";
    const t = out.payload?.temperature ?? out.payload?.temp ?? out.payload?.t;
    console.log(`[telemetry] ${deviceId} ${t ?? ""}`);
  } else if (out.kind === "flipper") {
    console.log(`[flipper] ${topic}`);
  } else {
    console.log(`[mqtt] ${topic}`);
  }
  broadcast(out);
});
