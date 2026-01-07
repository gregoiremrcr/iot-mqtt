<script lang="ts">
  import { onMount } from 'svelte';

  // ---- Config
  const WS_URL = import.meta.env.VITE_BRIDGE_WS_URL ?? 'ws://localhost:8080';
  const OFFLINE_MS = 60_000; // online si lastSeen < 60s
  // Ces deux ESP32 servent au bonus flipper. On les affiche dans la section Flipper, pas dans la grille météo.
  const FLIPPER_IDS = new Set(['esp32-06', 'esp32-07']);

  // Mapping deviceId -> nom de lieu
  const PLACES: Record<string, string> = {
    'device-001': 'Jardin',
    'device-002': 'Cantine',
    'device-003': 'Couloir',
    'device-004': 'Salle Zen',
    'device-005': 'Bureau',
    'esp32-01': 'Coin 01',
    'esp32-02': 'Coin 02',
    'esp32-03': 'Coin 03',
    'esp32-04': 'Coin 04',
    'esp32-05': 'Coin 05',
    'esp32-06': 'Flipper 06',
    'esp32-07': 'Flipper 07'
  };

  type Station = {
    deviceId: string;
    temperature?: number;
    humidity?: number;
    battery?: number;
    lastSeen?: number;
  };

  type FlipperEvent = {
    id: string;
    deviceId: string;
    kind: string; // buttons / tilt / plunger
    payload: any;
    ts: number;
  };

  let wsState: 'connecting' | 'open' | 'closed' | 'error' = 'connecting';
  let stations = new Map<string, Station>();

  let now = Date.now();
  const tick = setInterval(() => (now = Date.now()), 1000);

  // UI state
  let filter = 'all';
  let showOffline = false;

  // Flipper UI
  let lastFlipper: FlipperEvent | null = null;
  let flipperTimeline: FlipperEvent[] = [];
  let flipperCounts = new Map<string, number>(); // key: `${deviceId}:${button}`

  function cleanup() {
    clearInterval(tick);
  }

  // ---- Helpers: robust parsing (keeps last valid values)
  function extractNumber(v: any): number | null {
    if (v === null || v === undefined) return null;
    if (typeof v === 'number' && Number.isFinite(v)) return v;
    if (typeof v === 'string') {
      const m = v.replace(',', '.').match(/-?\d+(\.\d+)?/);
      return m ? Number(m[0]) : null;
    }
    return null;
  }

  function pick(obj: any, keys: string[]) {
    for (const k of keys) if (obj && obj[k] !== undefined) return obj[k];
    return undefined;
  }

  function extractTelemetry(payload: any) {
    let p: any = payload;

    // If string that looks like JSON -> parse best effort
    if (typeof p === 'string') {
      try {
        p = JSON.parse(p);
      } catch {
        // keep as string
      }
    }

    // Unwrap common nesting
    if (p && typeof p === 'object') p = p.telemetry ?? p.data ?? p.sensors ?? p;

    // If still string -> only accept if it clearly looks like weather
    if (typeof p === 'string') {
      const s = p.trim().toLowerCase();
      const looksLikeSingleNumber = /^-?\d+([.,]\d+)?\s*(°c|c)?$/.test(s);
      const hasWeatherKeyword = /(temp|temperature|humid|humidity|batt|battery|vbat)/.test(s);

      if (!looksLikeSingleNumber && !hasWeatherKeyword) {
        return { temperature: null, humidity: null, battery: null, ok: false };
      }

      const temp = extractNumber(p);
      return { temperature: temp, humidity: null, battery: null, ok: temp !== null };
    }

    const temperature = extractNumber(pick(p, ['temperature', 'temp', 'tempC', 'temp_c', 't', 'température']));
    const humidity = extractNumber(pick(p, ['humidity', 'hum', 'humPct', 'h', 'humidite', 'humidité']));
    const battery = extractNumber(pick(p, ['battery', 'batt', 'batteryPct', 'vbat', 'bat']));

    const ok = temperature !== null || humidity !== null || battery !== null;
    return { temperature, humidity, battery, ok };
  }

  function clampTelemetry(t: { temperature: number | null; humidity: number | null; battery: number | null; ok: boolean }) {
    const out = { ...t };
    if (out.temperature != null && (out.temperature < -30 || out.temperature > 60)) out.temperature = null;
    if (out.humidity != null && (out.humidity < 0 || out.humidity > 100)) out.humidity = null;
    if (out.battery != null && (out.battery < 0 || out.battery > 100)) out.battery = null;
    out.ok = out.temperature != null || out.humidity != null || out.battery != null;
    return out;
  }

  function isOnline(s: Station) {
    return (s.lastSeen ?? 0) && now - (s.lastSeen ?? 0) < OFFLINE_MS;
  }

  function upsertStation(deviceId: string, t: { temperature: number | null; humidity: number | null; battery: number | null }, ts: number) {
    const cur = stations.get(deviceId) ?? { deviceId };
    const next: Station = {
      ...cur,
      // never overwrite valid with null/undefined
      temperature: (t.temperature ?? cur.temperature) as any,
      humidity: (t.humidity ?? cur.humidity) as any,
      battery: (t.battery ?? cur.battery) as any,
      lastSeen: ts
    };
    stations.set(deviceId, next);
    // force reactivity
    stations = new Map(stations);
  }

  function parseDeviceIdFromTopic(topic: string) {
    // classroom/<deviceId>/telemetry  OR flipper/<deviceId>/<kind>
    const parts = topic.split('/');
    if (parts[0] === 'classroom') return parts[1] ?? 'unknown';
    if (parts[0] === 'flipper') return parts[1] ?? 'unknown';
    return parts[1] ?? parts[0] ?? 'unknown';
  }

  function labelFor(id: string) {
    return PLACES[id] ?? id;
  }

  function secondsAgo(ts?: number) {
    if (!ts) return '—';
    const s = Math.max(0, Math.round((now - ts) / 1000));
    if (s < 60) return `${s}s`;
    const m = Math.round(s / 60);
    return `${m}m`;
  }

  // derived lists
  $: allStations = Array.from(stations.values());

  // Stations météo (on exclut les ESP flipper). On n'affiche que les stations ayant au moins une valeur valide.
  $: weatherStations = allStations.filter(
    (s) => !FLIPPER_IDS.has(s.deviceId) && (s.temperature !== undefined || s.humidity !== undefined || s.battery !== undefined)
  );

  $: visibleStations = weatherStations
    .filter((s) => (showOffline ? true : isOnline(s)))
    .filter((s) => (filter === 'all' ? true : s.deviceId === filter))
    .sort((a, b) => labelFor(a.deviceId).localeCompare(labelFor(b.deviceId)));

  $: onlineStations = weatherStations.filter(isOnline);

  $: avgTemp = (() => {
    const withT = onlineStations.filter((s) => s.temperature != null);
    if (withT.length === 0) return null;
    return withT.reduce((acc, s) => acc + (s.temperature as number), 0) / withT.length;
  })();

  $: avgHum = (() => {
    const withH = onlineStations.filter((s) => s.humidity != null);
    if (withH.length === 0) return null;
    return withH.reduce((acc, s) => acc + (s.humidity as number), 0) / withH.length;
  })();

  function wsBadge() {
    if (wsState === 'open') return 'Connecté';
    if (wsState === 'connecting') return 'Connexion…';
    if (wsState === 'closed') return 'Fermé';
    return 'Erreur';
  }

  onMount(() => {
    let ws: WebSocket | null = null;
    let retry: any = null;

    const connect = () => {
      wsState = 'connecting';
      ws = new WebSocket(WS_URL);

      ws.onopen = () => (wsState = 'open');
      ws.onclose = () => {
        wsState = 'closed';
        retry = setTimeout(connect, 900);
      };
      ws.onerror = () => (wsState = 'error');

      ws.onmessage = (ev) => {
        // Some environments may not fire onopen reliably; if we receive data, we're open.
        wsState = 'open';

        let msg: any;
        try {
          msg = JSON.parse(ev.data);
        } catch {
          return;
        }

        const topic: string = msg.topic ?? '';
        const ts: number = msg.ts ?? Date.now();
        const payload = msg.payload ?? msg.data ?? msg.message ?? msg;

        // Flipper
        if (topic.startsWith('flipper/')) {
          const deviceId = parseDeviceIdFromTopic(topic);
          const kind = topic.split('/')[2] ?? 'event';
          // construit une clé unique (ts seul peut se dupliquer à la milliseconde)
          const parsed = typeof payload === 'string' ? (() => { try { return JSON.parse(payload); } catch { return null; } })() : payload;
          const seq = parsed?.seq;
          const id = `${deviceId}:${kind}:${seq ?? ''}:${ts}:${Math.random().toString(16).slice(2)}`;
          const fe: FlipperEvent = { id, deviceId, kind, payload, ts };
          lastFlipper = fe;
          flipperTimeline = [fe, ...flipperTimeline].slice(0, 20);

          // count buttons if payload carries a button id / name (best-effort)
          const p = parsed ?? payload;

          const btn = p?.button ?? p?.btn ?? p?.name ?? p?.id;
          if (btn !== undefined) {
            const key = `${deviceId}:${String(btn)}`;
            flipperCounts.set(key, (flipperCounts.get(key) ?? 0) + 1);
            flipperCounts = new Map(flipperCounts);
          }
          return;
        }

        // Telemetry
        if (topic.includes('/telemetry')) {
          const deviceId = parseDeviceIdFromTopic(topic);
          const t = clampTelemetry(extractTelemetry(payload));

          // IMPORTANT: ignore empty / non-parseable telemetry → avoids ghost offline stations
          if (!t.ok) return;

          upsertStation(deviceId, t, ts);
        }
      };
    };

    connect();

    return () => {
      cleanup();
      if (retry) clearTimeout(retry);
      if (ws) ws.close();
    };
  });
</script>

<svelte:head>
  <title>Néo Météo</title>
</svelte:head>

<div class="bg">
  <div class="container">
    <header class="header card">
      <div class="title">
        <div class="logo">⛅</div>
        <div>
          <h1>Néo Météo</h1>
          <p class="sub">Données temps réel (MQTT → WebSocket)</p>
        </div>
      </div>

      <div class="status">
        <span class="pill"><span class="dot {wsState}"></span> {wsBadge()}</span>
        <span class="pill ghost">Online &lt; {Math.round(OFFLINE_MS / 1000)}s</span>
      </div>
    </header>

    <section class="toolbar card">
      <div class="left">
        <label class="klabel" for="placeSelect">Lieu</label>
        <select id="placeSelect" bind:value={filter} class="kselect">
          <option value="all">Tous</option>
          {#each weatherStations as s (s.deviceId)}
            <option value={s.deviceId}>{labelFor(s.deviceId)}</option>
          {/each}
        </select>

        <label class="ktoggle">
          <input type="checkbox" bind:checked={showOffline} />
          <span>Afficher offline</span>
        </label>
      </div>

      <div class="right">
        <div class="bigpill">
          <span class="metric">🌡️ {avgTemp !== null ? `${avgTemp.toFixed(1)}°C` : '—'}</span>
          <span class="sep">•</span>
          <span class="metric">💧 {avgHum !== null ? `${avgHum.toFixed(0)}%` : '—'}</span>
        </div>
      </div>
    </section>

    <main class="grid">
      {#if visibleStations.length === 0}
        <div class="card empty">
          <h2>En attente de mesures</h2>
          <p class="sub">Vérifie que le bridge tourne et reçoit <code>classroom/+/telemetry</code>.</p>
        </div>
      {/if}

      {#each visibleStations as s (s.deviceId)}
        <article class="card station {isOnline(s) ? 'online' : 'offline'}">
          <div class="stationTop">
            <div class="place">{labelFor(s.deviceId)}</div>
            <div class="badge">{isOnline(s) ? 'Online' : 'Offline'}</div>
          </div>

          <div class="mainValue">
            <div class="temp">
              {#if s.temperature !== undefined}
                {Number(s.temperature).toFixed(1)}°
              {:else}
                —
              {/if}
            </div>
            <div class="rightCol">
              <div class="small">Dernière maj</div>
              <div class="small strong">{secondsAgo(s.lastSeen)}</div>
            </div>
          </div>

          <div class="mini">
            <div class="miniItem"><span class="icon">💧</span><span>{s.humidity !== undefined ? `${Number(s.humidity).toFixed(0)}%` : '—'}</span></div>
            <div class="miniItem"><span class="icon">🔋</span><span>{s.battery !== undefined ? `${Number(s.battery).toFixed(0)}%` : '—'}</span></div>
            <div class="miniItem"><span class="icon">⏱️</span><span>{isOnline(s) ? 'OK' : 'Stale'}</span></div>
          </div>
        </article>
      {/each}
    </main>

    <section class="card flipper">
      <div class="flTitle">
        <h2>Flipper</h2>
        <p class="sub">Dernier event + timeline (20)</p>
      </div>

      <div class="flGrid">
        <div class="flCard">
          <h3>Dernier event</h3>
          {#if lastFlipper}
            <div class="flLine"><b>Device</b><span>{labelFor(lastFlipper.deviceId)}</span></div>
            <div class="flLine"><b>Type</b><span>{lastFlipper.kind}</span></div>
            <div class="flLine"><b>Quand</b><span>{secondsAgo(lastFlipper.ts)} ago</span></div>
            <pre class="pre">{JSON.stringify(lastFlipper.payload, null, 2)}</pre>
          {:else}
            <p class="sub">En attente d’events <code>flipper/+/+</code>…</p>
          {/if}
        </div>

        <div class="flCard">
          <h3>Compteurs</h3>
          {#if flipperCounts.size === 0}
            <p class="sub">Aucun compteur détecté (si le payload ne contient pas <code>button</code>/<code>btn</code>).</p>
          {:else}
            <ul class="counts">
              {#each Array.from(flipperCounts.entries()).sort((a, b) => b[1] - a[1]) as [k, v] (k)}
                <li><span class="chip">{k}</span><span class="count">{v}</span></li>
              {/each}
            </ul>
          {/if}
        </div>

        <div class="flCard">
          <h3>Timeline</h3>
          {#if flipperTimeline.length === 0}
            <p class="sub">Rien pour l’instant…</p>
          {:else}
            <ul class="timeline">
              {#each flipperTimeline as e (e.id)}
                <li>
                  <span class="t">{secondsAgo(e.ts)}</span>
                  <span class="chip">{e.deviceId}</span>
                  <span class="chip soft">{e.kind}</span>
                </li>
              {/each}
            </ul>
          {/if}
        </div>
      </div>
    </section>

    <footer class="footer">
      <span>MQTT • WS • Svelte</span>
    </footer>
  </div>
</div>

<style>
  :global(body) {
    margin: 0;
    font-family: ui-sans-serif, system-ui, -apple-system, Segoe UI, Roboto, Arial;
  }

  .bg {
    min-height: 100vh;
    background:
      radial-gradient(900px 600px at 20% 10%, rgba(121, 180, 255, .28), transparent 60%),
      radial-gradient(900px 600px at 80% 0%, rgba(255, 210, 140, .22), transparent 60%),
      radial-gradient(900px 700px at 60% 90%, rgba(140, 255, 210, .16), transparent 60%),
      linear-gradient(180deg, #0b1220 0%, #0b1830 60%, #0a1020 100%);
    color: #eaf2ff;
  }

  .container {
    max-width: 1100px;
    margin: 0 auto;
    padding: 22px 16px 34px;
  }

  .card {
    background: rgba(255, 255, 255, .06);
    border: 1px solid rgba(255, 255, 255, .10);
    border-radius: 18px;
    box-shadow: 0 18px 60px rgba(0, 0, 0, .35);
    backdrop-filter: blur(10px);
  }

  .header {
    padding: 18px 18px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 16px;
  }

  .title {
    display: flex;
    align-items: center;
    gap: 14px;
  }

  .logo {
    width: 52px;
    height: 52px;
    display: flex;
    align-items: center;
    justify-content: center;
    border-radius: 14px;
    background: rgba(121, 180, 255, .14);
    border: 1px solid rgba(121, 180, 255, .25);
    font-size: 22px;
  }

  h1 {
    margin: 0;
    font-size: 18px;
    letter-spacing: .3px;
    font-weight: 750;
  }

  .sub {
    margin: 4px 0 0;
    opacity: .75;
    font-size: 12px;
    color: #cfe1ff;
  }

  .status {
    display: flex;
    gap: 10px;
    flex-wrap: wrap;
    justify-content: flex-end;
  }

  .pill {
    padding: 8px 12px;
    border-radius: 999px;
    border: 1px solid rgba(255, 255, 255, .14);
    background: rgba(255, 255, 255, .06);
    font-size: 12px;
    color: #eaf2ff;
    display: inline-flex;
    align-items: center;
    gap: 8px;
  }

  .pill.ghost {
    opacity: .85;
  }

  .dot {
    width: 8px;
    height: 8px;
    border-radius: 999px;
    background: rgba(255, 255, 255, .35);
  }

  .dot.open { background: rgba(140, 255, 210, .85); }
  .dot.connecting { background: rgba(255, 210, 140, .85); }
  .dot.closed { background: rgba(255, 255, 255, .35); }
  .dot.error { background: rgba(255, 120, 120, .85); }

  .toolbar {
    margin-top: 14px;
    padding: 14px 16px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    flex-wrap: wrap;
  }

  .left {
    display: flex;
    align-items: center;
    gap: 10px;
    flex-wrap: wrap;
  }

  .klabel {
    font-size: 12px;
    opacity: .85;
    color: #cfe1ff;
  }

  .kselect {
    padding: 10px 12px;
    border-radius: 12px;
    border: 1px solid rgba(255, 255, 255, .14);
    background: rgba(10, 16, 32, .45);
    color: #eaf2ff;
    outline: none;
  }

  .ktoggle {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 8px 10px;
    border-radius: 12px;
    border: 1px solid rgba(255, 255, 255, .14);
    background: rgba(255, 255, 255, .06);
    font-size: 12px;
    color: #eaf2ff;
  }

  .bigpill {
    padding: 10px 14px;
    border-radius: 14px;
    border: 1px solid rgba(255, 255, 255, .14);
    background: rgba(255, 255, 255, .06);
    font-weight: 650;
    display: flex;
    gap: 10px;
    align-items: center;
  }

  .metric { white-space: nowrap; }
  .sep { opacity: .55; }

  .grid {
    margin-top: 14px;
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
    gap: 14px;
  }

  .station {
    padding: 16px;
    position: relative;
    overflow: hidden;
  }

  .station::before {
    content: "";
    position: absolute;
    inset: -60px -80px auto auto;
    width: 220px;
    height: 220px;
    border-radius: 999px;
    background: rgba(121, 180, 255, .10);
    filter: blur(2px);
    transform: rotate(18deg);
  }

  .station.online {
    border-color: rgba(140, 255, 210, .22);
  }

  .station.offline {
    opacity: .55;
  }

  .stationTop {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 10px;
    position: relative;
    z-index: 1;
  }

  .place {
    font-weight: 750;
    letter-spacing: .2px;
  }

  .badge {
    font-size: 11px;
    padding: 6px 10px;
    border-radius: 999px;
    border: 1px solid rgba(255, 255, 255, .14);
    background: rgba(255, 255, 255, .06);
  }

  .mainValue {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    margin-top: 14px;
    position: relative;
    z-index: 1;
  }

  .temp {
    font-size: 44px;
    font-weight: 800;
    letter-spacing: -0.6px;
  }

  .rightCol {
    text-align: right;
  }

  .small {
    font-size: 12px;
    opacity: .75;
    color: #cfe1ff;
  }

  .strong {
    opacity: 1;
    color: #eaf2ff;
    font-weight: 700;
  }

  .mini {
    margin-top: 12px;
    display: flex;
    gap: 10px;
    position: relative;
    z-index: 1;
  }

  .miniItem {
    flex: 1;
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 10px 12px;
    border-radius: 14px;
    border: 1px solid rgba(255, 255, 255, .12);
    background: rgba(10, 16, 32, .35);
    font-size: 12px;
    color: #eaf2ff;
  }

  .icon {
    opacity: .8;
    margin-right: 8px;
  }

  .empty {
    padding: 20px;
    text-align: center;
  }

  code {
    background: rgba(255, 255, 255, .08);
    padding: 2px 6px;
    border-radius: 8px;
    border: 1px solid rgba(255, 255, 255, .12);
    color: #eaf2ff;
  }

  .flipper {
    margin-top: 14px;
    padding: 16px;
  }

  .flTitle h2 {
    margin: 0;
    font-size: 16px;
    font-weight: 750;
  }

  .flGrid {
    margin-top: 12px;
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
    gap: 12px;
  }

  .flCard {
    padding: 14px;
    border-radius: 16px;
    border: 1px solid rgba(255, 255, 255, .12);
    background: rgba(10, 16, 32, .35);
  }

  .flCard h3 {
    margin: 0 0 10px;
    font-size: 13px;
    font-weight: 700;
    color: #cfe1ff;
  }

  .flLine {
    display: flex;
    gap: 8px;
    margin: 6px 0;
    font-size: 13px;
  }

  .pre {
    margin-top: 10px;
    padding: 10px;
    border-radius: 12px;
    background: rgba(0, 0, 0, .22);
    border: 1px solid rgba(255, 255, 255, .10);
    overflow: auto;
    max-height: 180px;
    font-size: 12px;
    color: #eaf2ff;
  }

  .chip {
    display: inline-flex;
    padding: 4px 10px;
    border-radius: 999px;
    border: 1px solid rgba(255, 255, 255, .14);
    background: rgba(255, 255, 255, .06);
    font-size: 12px;
    color: #eaf2ff;
  }

  .chip.soft {
    opacity: .85;
  }

  .counts {
    list-style: none;
    padding: 0;
    margin: 0;
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .counts li {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 10px;
  }

  .count {
    font-weight: 900;
  }

  .timeline {
    list-style: none;
    padding: 0;
    margin: 0;
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .timeline li {
    display: flex;
    gap: 8px;
    align-items: center;
    flex-wrap: wrap;
  }

  .t {
    font-size: 12px;
    opacity: .7;
    width: 44px;
  }

  .footer {
    margin-top: 18px;
    text-align: center;
    font-size: 12px;
    opacity: .7;
    color: #cfe1ff;
  }
</style>
