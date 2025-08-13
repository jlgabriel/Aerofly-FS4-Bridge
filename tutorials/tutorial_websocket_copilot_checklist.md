# Websocket Tutorial: Co‑Pilot Checklist Dashboard

> What we'll build: A browser dashboard that connects to the Bridge via WebSocket and displays comprehensive takeoff/landing checklist items using canonical variables. Features real-time flight data display, intelligent checklist validation, and detailed explanations of each criterion. No build tools or frameworks—just one HTML file with educational content.

## Prerequisites
- Aerofly FS4 with AeroflyBridge.dll installed
- WebSocket interface enabled (`AEROFLY_BRIDGE_WS_ENABLE=1`)
- A modern web browser

## What You'll Learn
- Connect to the WebSocket data stream (`ws://localhost:8765`)
- Read canonical variables from `data.variables`
- Convert units (m/s to knots, radians to degrees, m to feet) for display
- Implement a comprehensive rule engine for flight phase checklists
- Display real-time flight data with proper formatting in organized rows
- Create educational content that explains the reasoning behind each checklist item
- Handle compass heading conversion from mathematical to aviation format

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
    body { margin: 0; padding: 20px; font-family: system-ui, Segoe UI, Roboto, Arial, sans-serif; color: var(--fg); background: radial-gradient(1200px 800px at 70% -10%, #1b2a4a, var(--bg)); }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 16px; }
    .card { background: var(--card); border-radius: 12px; padding: 16px; box-shadow: 0 8px 24px rgba(0,0,0,0.25); }
    h1 { font-size: 20px; margin: 0 0 12px 0; letter-spacing: .5px; }
    .status { font-size: 12px; color: var(--muted); margin-bottom: 8px; }
    .pill { display: inline-block; padding: 2px 8px; border-radius: 999px; font-size: 12px; }
    .pill.ok { background: rgba(0,210,106,0.15); color: var(--ok); border: 1px solid rgba(0,210,106,0.35); }
    .pill.warn { background: rgba(255,170,0,0.15); color: var(--warn); border: 1px solid rgba(255,170,0,0.35); }
    .pill.bad { background: rgba(255,59,59,0.15); color: var(--bad); border: 1px solid rgba(255,59,59,0.35); }
    .flight-data { font-family: Consolas, Menlo, monospace; font-size: 13px; }
    .data-row { display: flex; justify-content: space-between; padding: 6px 0; border-bottom: 1px dashed rgba(255,255,255,0.08); }
    .data-row:last-child { border-bottom: none; }
    .light { display: flex; align-items: center; gap: 10px; padding: 6px 0; }
    .dot { width: 12px; height: 12px; border-radius: 50%; background: #777; box-shadow: 0 0 0 2px rgba(255,255,255,0.06) inset; }
    .dot.ok { background: var(--ok); box-shadow: 0 0 16px rgba(0,210,106,0.7); }
    .dot.warn { background: var(--warn); box-shadow: 0 0 16px rgba(255,170,0,0.7); }
    .dot.bad { background: var(--bad); box-shadow: 0 0 16px rgba(255,59,59,0.7); }
    .section-title { margin: 12px 0 6px; font-size: 12px; color: var(--muted); text-transform: uppercase; letter-spacing: .12em; }
    .checklist-info { margin-top: 16px; padding: 12px; background: rgba(255,255,255,0.03); border-radius: 8px; border-left: 3px solid var(--muted); }
    .checklist-info h3 { margin: 0 0 8px 0; font-size: 13px; color: var(--fg); }
    .checklist-info ul { margin: 6px 0; padding-left: 16px; }
    .checklist-info li { margin: 3px 0; font-size: 12px; color: var(--muted); line-height: 1.4; }
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
      <div class="flight-data" id="flightData"></div>
    </div>
    <div class="card">
      <div class="section-title">Takeoff Checklist</div>
      <div id="takeoffList"></div>
      <div class="section-title">Landing Checklist</div>
      <div id="landingList"></div>
    </div>
  </div>

  <div class="checklist-info">
    <h3>Takeoff Checklist Criteria:</h3>
    <ul>
      <li><strong>Flaps configured:</strong> Any flap extension (> 0%) for takeoff lift</li>
      <li><strong>Throttle > 60%:</strong> Adequate power for acceleration and rotation</li>
      <li><strong>IAS alive > 30 kt:</strong> Airspeed indicator functioning, sufficient speed for controls</li>
      <li><strong>Positive pitch (+5° to +15°):</strong> Nose up attitude for climb after rotation</li>
      <li><strong>Low altitude (< 1000 ft):</strong> Still in takeoff phase, not yet in cruise</li>
    </ul>
    
    <h3>Landing Checklist Criteria:</h3>
    <ul>
      <li><strong>Stable descent (< 2000 fpm):</strong> Controlled descent rate, not too steep</li>
      <li><strong>Flaps set for landing (≥ 15%):</strong> Increased lift and drag for approach</li>
      <li><strong>IAS below 120 kt:</strong> Appropriate approach speed for most aircraft</li>
      <li><strong>Landing pitch (+2° to +8°):</strong> Slight nose-up attitude for proper flare</li>
      <li><strong>Power reduced (< 50%):</strong> Reduced thrust for controlled descent</li>
      <li><strong>Descent rate ≤ 1000 fpm:</strong> Safe vertical speed for landing approach</li>
    </ul>
  </div>

  <footer>
    Uses canonical variables: <code>Aircraft.Flaps</code>, <code>Aircraft.Throttle</code>, 
    <code>Aircraft.IndicatedAirspeed</code>, <code>Aircraft.Altitude</code>, 
    <code>Aircraft.VerticalSpeed</code>, <code>Aircraft.Pitch</code>, 
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

    function light(label, state) {
      const row = document.createElement('div'); row.className = 'light';
      const d = document.createElement('span'); d.className = 'dot ' + state;
      const t = document.createElement('span'); t.textContent = label;
      row.appendChild(d); row.appendChild(t);
      return row;
    }

    function render(data) {
      const v = (data && data.variables) || {};

      const flaps = Number(v['Aircraft.Flaps'] || 0);
      const throttle = Number(v['Aircraft.Throttle'] || 0);
      const ias = Number(v['Aircraft.IndicatedAirspeed'] || 0) * MPS_TO_KT; // m/s → kt
      const alt = Number(v['Aircraft.Altitude'] || 0) * M_TO_FT; // m → ft
      const vs = Number(v['Aircraft.VerticalSpeed'] || 0) * 196.850394; // m/s → fpm
      const pitch = Number(v['Aircraft.Pitch'] || 0) * RAD_TO_DEG; // rad → deg
      const hdgSrc = (v['Aircraft.MagneticHeading'] ?? v['Aircraft.TrueHeading'] ?? 0);
      const heading = compassHeadingFromCanonical(hdgSrc);

      // Flight data - each variable in its own row
      flightData.innerHTML = `
        <div class="data-row">
          <span>Heading</span><span>${heading.toFixed(0)}°</span>
        </div>
        <div class="data-row">
          <span>IAS</span><span>${ias.toFixed(0)} kt</span>
        </div>
        <div class="data-row">
          <span>Altitude</span><span>${alt.toFixed(0)} ft</span>
        </div>
        <div class="data-row">
          <span>Vertical Speed</span><span>${(vs>=0?'+':'') + vs.toFixed(0)} fpm</span>
        </div>
        <div class="data-row">
          <span>Pitch</span><span>${(pitch>=0?'+':'') + pitch.toFixed(1)}°</span>
        </div>
        <div class="data-row">
          <span>Flaps</span><span>${(flaps*100).toFixed(0)} %</span>
        </div>
        <div class="data-row">
          <span>Throttle</span><span>${(throttle*100).toFixed(0)} %</span>
        </div>
      `;

      // Takeoff checklist
      takeoffList.innerHTML = '';
      takeoffList.appendChild(light('Flaps configured', (flaps > 0) ? 'ok' : 'warn'));
      takeoffList.appendChild(light('Throttle > 60%', (throttle > 0.60) ? 'ok' : 'warn'));
      takeoffList.appendChild(light('IAS alive > 30 kt', (ias > 30) ? 'ok' : 'warn'));
      takeoffList.appendChild(light('Positive pitch (+5° to +15°)', (pitch > 5 && pitch < 15) ? 'ok' : 'warn'));
      takeoffList.appendChild(light('Low altitude (< 1000 ft)', (alt < 1000) ? 'ok' : 'warn'));

      // Landing checklist
      landingList.innerHTML = '';
      const stableDescent = (vs < 0 && Math.abs(vs) < 2000); // Descending and < 2000 fpm
      const flapsLanding = flaps >= 0.15;
      const landingPitch = (pitch > 2 && pitch < 8);
      const powerReduced = (throttle < 0.5);
      const vsOk = Math.abs(vs) <= 1000;
      
      landingList.appendChild(light('Stable descent (< 2000 fpm)', stableDescent ? 'ok' : 'warn'));
      landingList.appendChild(light('Flaps set for landing (≥ 15%)', flapsLanding ? 'ok' : 'warn'));
      landingList.appendChild(light('IAS below 120 kt', (ias < 120) ? 'ok' : 'warn'));
      landingList.appendChild(light('Landing pitch (+2° to +8°)', landingPitch ? 'ok' : 'warn'));
      landingList.appendChild(light('Power reduced (< 50%)', powerReduced ? 'ok' : 'warn'));
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

The dashboard connects to `ws://localhost:8765` and displays real-time flight data along with intelligent checklists:

### Flight Data Display
The left panel shows organized flight data in clean rows:
- **Heading**: Converted from mathematical radians to compass degrees (0°=North, CW) using the `compassHeadingFromCanonical()` function
- **IAS**: Indicated Airspeed converted from m/s to knots (×1.943844492)
- **Altitude**: Converted from meters to feet (×3.280839895)
- **Vertical Speed**: Converted from m/s to feet per minute (×196.850394) with + for climbs
- **Pitch**: Aircraft nose attitude in degrees (+ = nose up, - = nose down) with decimal precision
- **Flaps**: Extension percentage (0-100%) converted from decimal format
- **Throttle**: Power setting percentage (0-100%) converted from decimal format

### Checklist Logic (Right Panel)
The right panel contains two intelligent checklists with detailed explanations:

**Takeoff Checklist (5 items)**
- **Flaps configured**: Any flap extension (> 0%) - universal for all aircraft types
- **Throttle > 60%**: Adequate power for acceleration and rotation
- **IAS alive > 30 kt**: Airspeed indicator functioning, sufficient speed for controls
- **Positive pitch (+5° to +15°)**: Nose up attitude for climb after rotation
- **Low altitude (< 1000 ft)**: Still in takeoff phase, not yet in cruise

**Landing Checklist (6 items)**
- **Stable descent (< 2000 fpm)**: Controlled descent rate, not too steep (checks `vs < 0` and `Math.abs(vs) < 2000`)
- **Flaps set for landing (≥ 15%)**: Increased lift and drag for approach (`flaps >= 0.15`)
- **IAS below 120 kt**: Appropriate approach speed for most aircraft
- **Landing pitch (+2° to +8°)**: Slight nose-up attitude for proper flare (`pitch > 2 && pitch < 8`)
- **Power reduced (< 50%)**: Reduced thrust for controlled descent (`throttle < 0.5`)
- **Descent rate ≤ 1000 fpm**: Safe vertical speed for landing approach (`Math.abs(vs) <= 1000`)

### Status Indicators
- 🟢 **Green (OK)**: Parameter within safe/normal range
- 🟡 **Yellow (WARN)**: Parameter outside recommended range but not critical
- 🔴 **Red (BAD)**: Parameter in dangerous range requiring immediate attention

### Educational Features
The dashboard includes a comprehensive information panel below the checklists that explains:

**Takeoff Checklist Criteria:**
- Detailed explanation of why each parameter matters during takeoff
- Specific thresholds and their aviation safety reasoning
- Universal criteria that work across different aircraft types

**Landing Checklist Criteria:**
- Breakdown of approach and landing phase requirements
- Safety margins and why each check is critical
- Professional pilot procedures adapted for simulation

**Variable Reference:**
- Lists all canonical variables used: `Aircraft.Flaps`, `Aircraft.Throttle`, `Aircraft.IndicatedAirspeed`, `Aircraft.Altitude`, `Aircraft.VerticalSpeed`, `Aircraft.Pitch`, `Aircraft.MagneticHeading`/`Aircraft.TrueHeading`
- Helps users understand what data the Bridge provides

## Customize for Your Aircraft

The thresholds are designed to work universally but can be adjusted:

```javascript
// Example: Adjust for light aircraft
const flapsOk = (flaps > 0.10); // 10% minimum instead of any extension
const takeoffThrottle = (throttle > 0.75); // 75% instead of 60% for light aircraft
const approachSpeed = (ias < 90); // 90 kt instead of 120 kt for small GA aircraft
```

## Educational Value

The dashboard includes detailed explanations of each checklist item, making it valuable for:
- **Flight training**: Understanding what parameters matter during critical phases
- **Safety awareness**: Learning why each check is important
- **Aircraft familiarization**: Seeing how different aircraft behave
- **Procedural knowledge**: Building systematic approach to flight operations

## Troubleshooting
- **Connection failed**: Confirm Aerofly is running and WebSocket is enabled
- **No updates**: Start a flight, check browser console (F12) for errors
- **Values look odd**: Ensure Bridge version matches repo and canonical variables are working
- **Some checks always yellow**: Normal for certain aircraft types; adjust thresholds as needed

## Technical Notes

### Key Implementation Details
- **Unit Conversions**: Precise conversion constants for aviation units
  - `M_TO_FT = 3.280839895` (meters to feet)
  - `MPS_TO_KT = 1.943844492` (m/s to knots)  
  - `RAD_TO_DEG = 180 / Math.PI` (radians to degrees)
  - Custom vertical speed conversion: `× 196.850394` (m/s to fpm)

- **Compass Heading Logic**: The `compassHeadingFromCanonical()` function handles the conversion from mathematical heading (where 0° = East, CCW) to aviation compass heading (0° = North, CW)

- **Robust Variable Access**: Uses fallback logic `v['Aircraft.MagneticHeading'] ?? v['Aircraft.TrueHeading'] ?? 0` to handle different aircraft configurations

- **Educational Design**: Combines functional checklist with detailed explanations, making it valuable for flight training and understanding aviation procedures

- **Universal Compatibility**: Uses only reliable, universal variables available across all aircraft types, avoiding aircraft-specific variables like `Aircraft.OnGround` or `Aircraft.ParkingBrake`

---

🎯 **Mission Complete!** You now have a comprehensive, educational co‑pilot dashboard that demonstrates practical use of Bridge data while teaching proper flight procedures and safety awareness.