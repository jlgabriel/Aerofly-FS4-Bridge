# Web App Tutorial: Live Dashboard over WebSocket

Build a simple web dashboard that connects to the Aerofly Bridge over WebSocket, reads live telemetry, and sends commands.

## What you'll build

- A single HTML page that:
  - Connects to `ws://localhost:8765`
  - Displays basic flight data (altitude, IAS, heading)
  - Sends a command (e.g., throttle change)
  - Reconnects automatically if the connection drops

## Prerequisites

- Aerofly FS 4 with the Bridge DLL running
- WebSocket server enabled (default ON)
- Port: `8765` (change via `AEROFLY_BRIDGE_WS_PORT`)
- Any modern browser

## Data shape recap

Each message from the bridge is JSON with:

```json
{
  "schema": "aerofly-bridge-telemetry",
  "schema_version": 1,
  "timestamp": 1640995200000000,
  "timestamp_unit": "microseconds",
  "data_valid": 1,
  "variables": {
    "Aircraft.Altitude": 1066.8,
    "Aircraft.IndicatedAirspeed": 61.8,
    "Aircraft.TrueHeading": 1.57
  },
  "all_variables": [ 0.0, 1066.8, 0.76 ]
}
```

- Use `variables["Canonical.Name"]` for readability
- Use `all_variables[index]` when you need maximum speed

## Minimal HTML dashboard

Save as `dashboard.html` and open it in your browser.

```html
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <title>Aerofly Web Dashboard</title>
  <style>
    body { font-family: system-ui, sans-serif; margin: 2rem; }
    .grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 1rem; }
    .card { border: 1px solid #ddd; border-radius: 8px; padding: 1rem; }
    .ok { color: #2e7d32; } .warn { color: #ef6c00; } .err { color: #c62828; }
  </style>
</head>
<body>
  <h1>Aerofly Web Dashboard</h1>
  <div id="status" class="warn">Connecting…</div>

  <div class="grid">
    <div class="card"><h3>Altitude</h3><div id="alt">—</div></div>
    <div class="card"><h3>IAS</h3><div id="ias">—</div></div>
    <div class="card"><h3>Heading</h3><div id="hdg">—</div></div>
  </div>

  <h2>Command</h2>
  <div class="card">
    <label>Throttle (0.0–1.0)
      <input id="thr" type="number" step="0.05" min="0" max="1" value="0.5" />
    </label>
    <button id="send">Send</button>
    <div id="cmdStatus"></div>
  </div>

<script>
const WS_URL = `ws://${location.hostname}:8765`;
let ws = null;
let backoffMs = 500;
const maxBackoffMs = 5000;

function setStatus(text, cls) {
  const s = document.getElementById('status');
  s.textContent = text; s.className = cls;
}

function connect() {
  setStatus(`Connecting to ${WS_URL}…`, 'warn');
  ws = new WebSocket(WS_URL);

  ws.onopen = () => {
    setStatus('Connected', 'ok');
    backoffMs = 500;
  };

  ws.onmessage = (event) => {
    try {
      const frame = JSON.parse(event.data);
      if (frame.data_valid !== 1) return;
      const v = frame.variables || {};
      // Canonical variables
      const alt_m = v['Aircraft.Altitude'];
      const ias_mps = v['Aircraft.IndicatedAirspeed'];
      const hdg_rad = v['Aircraft.TrueHeading'];

      // Display (unit conversions for readability)
      document.getElementById('alt').textContent = `${Math.round(alt_m)} m`;
      document.getElementById('ias').textContent = `${(ias_mps * 1.94384).toFixed(1)} kt`;
      document.getElementById('hdg').textContent = `${(hdg_rad * 180/Math.PI).toFixed(0)}°`;
    } catch (e) {
      console.error('Bad frame', e);
    }
  };

  ws.onclose = () => {
    setStatus('Disconnected – retrying…', 'err');
    ws = null;
    setTimeout(connect, backoffMs);
    backoffMs = Math.min(backoffMs * 2, maxBackoffMs);
  };

  ws.onerror = (err) => {
    console.error('WS error', err);
  };
}

function sendCommand(name, value) {
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    showCmdStatus('Not connected', true);
    return;
  }
  const cmd = { variable: name, value: value };
  ws.send(JSON.stringify(cmd));
  showCmdStatus(`Sent: ${name}=${value}`);
}

function showCmdStatus(text, isError = false) {
  const n = document.getElementById('cmdStatus');
  n.textContent = text; n.className = isError ? 'err' : 'ok';
  setTimeout(() => { n.textContent = ''; n.className = ''; }, 2000);
}

// UI
window.addEventListener('load', () => {
  connect();
  document.getElementById('send').addEventListener('click', () => {
    const thr = parseFloat(document.getElementById('thr').value);
    const clamped = Math.max(0, Math.min(1, thr));
    sendCommand('Aircraft.Throttle', clamped);
  });
});
</script>
</body>
</html>
```

## Common issues

- Connection fails: check firewall and that the bridge is running
- Wrong port: set `AEROFLY_BRIDGE_WS_PORT` and restart the bridge
- No data: ensure Aerofly FS 4 is in a flight and `data_valid` is 1

## Next steps

- Add more variables from the [Variables Reference](variables_reference.md)
- Plot time series using a charting library
- Build a React/Vue front-end (the protocol stays the same)
