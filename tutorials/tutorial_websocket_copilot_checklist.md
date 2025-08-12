# Simple Tutorial #7: Co‑Pilot Checklist Dashboard (WebSocket)

> What we'll build: A browser dashboard that connects to the Bridge via WebSocket and lights up simple takeoff/landing checklist items using canonical variables. No build tools or frameworks—just one HTML file.

## Prerequisites
- Aerofly FS4 with AeroflyBridge.dll installed
- WebSocket interface enabled (`AEROFLY_BRIDGE_WS_ENABLE=1`)
- A modern web browser

## What You'll Learn
- Connect to the WebSocket data stream (`ws://localhost:8765`)
- Read canonical variables from `data.variables`
- Convert units and booleans for display
- Implement a simple rule engine for checklists

## Step 1: Create the HTML file

Create a file called `copilot_checklist.html` with the following content:

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Co‑Pilot Checklist</title>
  <style>
    :root {
      --ok: #00d26a;
      --warn: #ffaa00;
      --bad: #ff3b3b;
      --bg: #0e1420;
      --fg: #e6f0ff;
      --muted: #7f8da3;
      --card: #152038;
    }
    body {
      margin: 0; padding: 20px; font-family: system-ui, Segoe UI, Roboto, Arial, sans-serif;
      color: var(--fg); background: radial-gradient(1200px 800px at 70% -10%, #1b2a4a, var(--bg));
    }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 16px; }
    .card { background: var(--card); border-radius: 12px; padding: 16px; box-shadow: 0 8px 24px rgba(0,0,0,0.25); }
    h1 { font-size: 20px; margin: 0 0 12px 0; letter-spacing: .5px; }
    .status { font-size: 12px; color: var(--muted); margin-bottom: 8px; }
    .pill { display: inline-block; padding: 2px 8px; border-radius: 999px; font-size: 12px; }
    .pill.ok { background: rgba(0,210,106,0.15); color: var(--ok); border: 1px solid rgba(0,210,106,0.35); }
    .pill.warn { background: rgba(255,170,0,0.15); color: var(--warn); border: 1px solid rgba(255,170,0,0.35); }
    .pill.bad { background: rgba(255,59,59,0.15); color: var(--bad); border: 1px solid rgba(255,59,59,0.35); }
    .kv { display: grid; grid-template-columns: 1fr auto; gap: 6px; font-family: Consolas, Menlo, monospace; font-size: 13px; }
    .kv div { padding: 6px 0; border-bottom: 1px dashed rgba(255,255,255,0.08); }
    .light { display: flex; align-items: center; gap: 10px; padding: 6px 0; }
    .dot { width: 12px; height: 12px; border-radius: 50%; background: #777; box-shadow: 0 0 0 2px rgba(255,255,255,0.06) inset; }
    .dot.ok { background: var(--ok); box-shadow: 0 0 16px rgba(0,210,106,0.7); }
    .dot.warn { background: var(--warn); box-shadow: 0 0 16px rgba(255,170,0,0.7); }
    .dot.bad { background: var(--bad); box-shadow: 0 0 16px rgba(255,59,59,0.7); }
    .section-title { margin: 12px 0 6px; font-size: 12px; color: var(--muted); text-transform: uppercase; letter-spacing: .12em; }
    footer { margin-top: 16px; font-size: 12px; color: var(--muted); }
    code { background: #0b1322; padding: 2px 6px; border-radius: 6px; color: #b7cffb; }
  </style>
</head>
<body>
  <h1>Co‑Pilot Checklist</h1>
  <div class="status">WebSocket: <span id="wsStatus" class="pill">connecting…</span></div>

  <div class="grid">
    <div class="card">
      <div class="section-title">Flight Data</div>
      <div class="kv" id="flightData"></div>
    </div>
    <div class="card">
      <div class="section-title">Takeoff Checklist</div>
      <div id="takeoffList"></div>
      <div class="section-title">Landing Checklist</div>
      <div id="landingList"></div>
    </div>
  </div>

  <footer>
    Uses canonical variables: <code>Aircraft.OnGround</code>, <code>Aircraft.ParkingBrake</code>,
    <code>Aircraft.Flaps</code>, <code>Aircraft.Throttle</code>, <code>Aircraft.IndicatedAirspeed</code>,
    <code>Aircraft.Altitude</code>, <code>Aircraft.VerticalSpeed</code>,
    <code>Aircraft.MagneticHeading</code> / <code>Aircraft.TrueHeading</code>.
  </footer>

  <script>
    const wsUrl = 'ws://localhost:8765';
    let ws = null;
    let reconnectTimer = null;

    const el = (id) => document.getElementById(id);
    const wsStatus = el('wsStatus');
    const flightData = el('flightData');
    const takeoffList = el('takeoffList');
    const landingList = el('landingList');

    const M_TO_FT = 3.280839895;
    const MPS_TO_KT = 1.943844492;
    const RAD_TO_DEG = 180 / Math.PI;

    function setWsState(state) {
      wsStatus.textContent = state;
      wsStatus.className = 'pill ' + (state === 'connected' ? 'ok' : (state === 'connecting…' ? '' : 'bad'));
    }

    function compassHeadingFromCanonical(src) {
      const hdgMathDeg = (Math.abs(src) <= 6.5) ? (src * RAD_TO_DEG) % 360 : (src % 360);
      let heading = (90 - hdgMathDeg) % 360; if (heading < 0) heading += 360;
      return heading;
    }

    function kvRow(label, value) {
      const wrap = document.createElement('div');
      wrap.className = 'kv';
      const k = document.createElement('div'); k.textContent = label;
      const v = document.createElement('div'); v.textContent = value;
      wrap.appendChild(k); wrap.appendChild(v);
      return wrap;
    }

    function light(label, state) {
      const row = document.createElement('div'); row.className = 'light';
      const d = document.createElement('span'); d.className = 'dot ' + state;
      const t = document.createElement('span'); t.textContent = label;
      row.appendChild(d); row.appendChild(t);
      return row;
    }

    function render(data) {
      const v = (data && data.variables) || {};

      const onGround = (v['Aircraft.OnGround'] === 1);
      const parkingBrake = (v['Aircraft.ParkingBrake'] === 1);
      const flaps = Number(v['Aircraft.Flaps'] || 0);
      const throttle = Number(v['Aircraft.Throttle'] || 0);
      const ias = Number(v['Aircraft.IndicatedAirspeed'] || 0) * MPS_TO_KT; // m/s → kt
      const alt = Number(v['Aircraft.Altitude'] || 0) * M_TO_FT; // m → ft
      const vs = Number(v['Aircraft.VerticalSpeed'] || 0) * 196.850394; // m/s → fpm
      const hdgSrc = (v['Aircraft.MagneticHeading'] ?? v['Aircraft.TrueHeading'] ?? 0);
      const heading = compassHeadingFromCanonical(hdgSrc);

      // Flight data block
      flightData.innerHTML = '';
      flightData.appendChild(kvRow('On Ground', onGround ? 'YES' : 'NO'));
      flightData.appendChild(kvRow('Heading', heading.toFixed(0) + '°'));
      flightData.appendChild(kvRow('IAS', ias.toFixed(0) + ' kt'));
      flightData.appendChild(kvRow('Altitude', alt.toFixed(0) + ' ft'));
      flightData.appendChild(kvRow('Vertical Speed', (vs>=0?'+':'') + vs.toFixed(0) + ' fpm'));
      flightData.appendChild(kvRow('Flaps', (flaps*100).toFixed(0) + ' %'));
      flightData.appendChild(kvRow('Throttle', (throttle*100).toFixed(0) + ' %'));

      // Takeoff checklist rules
      // Goals: Parking brake OFF, Flaps in takeoff band (5–20%), Throttle advanced (>60%), IAS alive (>30 kt), On ground = YES
      takeoffList.innerHTML = '';
      takeoffList.appendChild(light('Parking Brake OFF', (!parkingBrake) ? 'ok' : 'bad'));
      const flapsOk = flaps >= 0.05 && flaps <= 0.20; // generic GA takeoff band
      takeoffList.appendChild(light('Flaps set (5–20%)', flapsOk ? 'ok' : (flaps === 0 ? 'warn' : 'bad')));
      takeoffList.appendChild(light('Throttle > 60%', (throttle > 0.60) ? 'ok' : 'warn'));
      takeoffList.appendChild(light('IAS alive > 30 kt', (ias > 30) ? 'ok' : 'warn'));
      takeoffList.appendChild(light('On runway/ground', onGround ? 'ok' : 'warn'));

      // Landing checklist rules
      // Goals: Gear DOWN (when applicable; proxy with OnGround OR low IAS), Flaps in landing band (20–100%), IAS below 120 kt, Descent rate within ±1000 fpm
      // Note: Generic variables set does not expose gear lever for all aircraft. We present intent-focused checks.
      landingList.innerHTML = '';
      const landingPhase = (!onGround && ias < 140 && alt < 5000);
      const flapsLanding = flaps >= 0.20; // simple heuristic
      const vsOk = Math.abs(vs) <= 1000 || onGround;
      landingList.appendChild(light('Approach phase detected', landingPhase ? 'ok' : 'warn'));
      landingList.appendChild(light('Flaps set for landing (≥ 20%)', flapsLanding ? 'ok' : 'warn'));
      landingList.appendChild(light('IAS below 120 kt', (ias < 120 || onGround) ? 'ok' : 'warn'));
      landingList.appendChild(light('Descent rate ≤ 1000 fpm', vsOk ? 'ok' : 'bad'));
    }

    function connect() {
      try {
        setWsState('connecting…');
        ws = new WebSocket(wsUrl);
        ws.onopen = () => { setWsState('connected'); if (reconnectTimer) { clearInterval(reconnectTimer); reconnectTimer = null; } };
        ws.onmessage = (ev) => { try { const data = JSON.parse(ev.data); render(data); } catch (e) {} };
        ws.onclose = () => { setWsState('disconnected'); if (!reconnectTimer) reconnectTimer = setInterval(connect, 3000); };
        ws.onerror = () => { setWsState('error'); };
      } catch (_) {
        setWsState('error');
        if (!reconnectTimer) reconnectTimer = setInterval(connect, 3000);
      }
    }

    connect();
  </script>
</body>
</html>
```

## Step 2: Run it
1. Start Aerofly FS4 with the Bridge running.
2. Ensure WebSocket is enabled. If needed, set environment variables and restart:
   - `AEROFLY_BRIDGE_WS_ENABLE=1`
   - `AEROFLY_BRIDGE_WS_PORT=8765` (optional)
3. Open the `copilot_checklist.html` file in your browser.

## How It Works
- The page connects to `ws://localhost:8765` and listens for JSON frames.
- It reads canonical variables from `data.variables` and applies simple rules:
  - Takeoff: brake off, flaps 5–20%, throttle > 60%, IAS > 30 kt, on ground.
  - Landing: approach detected when airspeed/altitude suggest arrival; flaps ≥ 20%, IAS < 120 kt, descent rate ≤ 1000 fpm.
- Heading is normalized from radians (math: 0°=East, CCW+) to compass (0°=North, CW).

## Customize the Rules
Edit the thresholds in the script for your aircraft type. For example:
- GA aircraft might prefer flaps 10–20% for takeoff; jets may use 0–10%.
- Tighten the landing IAS threshold for light aircraft (e.g., 90–100 kt).

## Troubleshooting
- Connection failed: confirm Aerofly is running and WebSocket is enabled.
- No updates: start a flight, check the browser console (F12) for errors.
- Values look odd: ensure your Bridge version matches the repo and that canonical variables are enabled.

---

🎯 Mission Complete! You now have a simple, extensible web-based co‑pilot dashboard that demonstrates how to consume Bridge data and drive practical UI logic in your own applications.


