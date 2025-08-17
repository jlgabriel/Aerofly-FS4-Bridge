# Websocket Tutorial: Airspeed Envelope Tape

> What we'll build: A web instrument that shows a live airspeed tape with performance envelopes (white/green/yellow/red arcs) using canonical variables from the Bridge. Single HTML file, no frameworks.

## Prerequisites
- Aerofly FS4 with AeroflyBridge.dll installed
- WebSocket interface enabled (`AEROFLY_BRIDGE_WS_ENABLE=1`)
- Any modern browser

## What You'll Learn
- Consume Bridge data via WebSocket `ws://localhost:8765`
- Prefer canonical variables: `Aircraft.IndicatedAirspeed` and `Performance.Speed.*`, with index fallback when a canonical key is missing
- Convert units and render a simple instrument with `<canvas>`

## Step 1: Create the HTML file

Create a file named `airspeed_envelope.html`:

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Airspeed Envelope Tape</title>
  <style>
    :root { --bg:#0e1420; --fg:#e6f0ff; --muted:#7f8da3; --card:#141d33; }
    body { margin:0; padding:24px; background: radial-gradient(1000px 700px at 70% -10%, #1b2a4a, var(--bg)); color:var(--fg); font-family: system-ui, Segoe UI, Roboto, Arial, sans-serif; }
    h1 { margin:0 0 6px 0; font-size:20px; letter-spacing:.4px; }
    .status { font-size:12px; color:var(--muted); margin-bottom:12px; }
    .wrap { display:flex; gap:16px; flex-wrap:wrap; }
    .card { background:var(--card); border-radius:12px; padding:16px; box-shadow:0 8px 24px rgba(0,0,0,.25); }
    .big { font-size:42px; font-weight:700; }
    .row { display:flex; gap:16px; align-items:center; }
    .kv { font-family:Consolas, Menlo, monospace; font-size:13px; color:var(--muted); }
    canvas { background: #0b1322; border-radius:10px; box-shadow: inset 0 0 0 1px rgba(255,255,255,0.05); }
    code { background:#0b1322; color:#b7cffb; padding:2px 6px; border-radius:6px; }
  </style>
</head>
<body>
  <h1>Airspeed Envelope Tape</h1>
  <div class="status">WebSocket: <span id="ws">connecting…</span></div>

  <div class="wrap">
    <div class="card">
      <canvas id="tape" width="720" height="160"></canvas>
    </div>
    <div class="card">
      <div class="row">
        <div class="big" id="ias">--</div>
        <div class="kv" id="speeds"></div>
      </div>
    </div>
  </div>

  <p class="status">
    Uses canonical variables: <code>Aircraft.IndicatedAirspeed</code>,
    <code>Performance.Speed.VS0</code>, <code>VFE</code>, <code>VNO</code>, <code>VNE</code>, <code>VAPP</code>.
  </p>

  <script>
    const MPS_TO_KT = 1.943844492;
    const wsUrl = 'ws://localhost:8765';

    const el = (id) => document.getElementById(id);
    const wsEl = el('ws');
    const tape = /** @type {HTMLCanvasElement} */(el('tape'));
    const ctx = tape.getContext('2d');
    const iasEl = el('ias');
    const speedsEl = el('speeds');

    let reconnectTimer = null;
    let latest = {
      iasKt: 0,
      VS0: NaN, VFE: NaN, VNO: NaN, VNE: NaN, VAPP: NaN
    };

    function setWs(state){ wsEl.textContent = state; }

    function drawTape() {
      const w = tape.width, h = tape.height;
      ctx.clearRect(0,0,w,h);

      // Gauge bounds
      const margin = 24;
      const barX = margin, barY = h/2 - 22, barW = w - margin*2, barH = 44;

      // Determine scale based on VNE if available, else dynamic around IAS
      const maxRef = isFinite(latest.VNE) ? latest.VNE : Math.max(160, Math.ceil(Math.max(120, latest.iasKt * 1.6)/10)*10);
      const minRef = 0;

      // Helper: map kt -> x
      const toX = (kt) => barX + Math.max(0, Math.min(1, (kt - minRef) / (maxRef - minRef))) * barW;

      // Background
      ctx.fillStyle = '#0a1120';
      ctx.fillRect(barX, barY, barW, barH);

      // Zones: white (VS0..VFE), green (VS1?..VNO), yellow (VNO..VNE), red (VNE..)
      // We may not have VS1; use VS0 as start of green for simplicity.
      const VS0 = latest.VS0, VFE = latest.VFE, VNO = latest.VNO, VNE = latest.VNE;

      // White arc (flap operating range)
      if (isFinite(VS0) && isFinite(VFE) && VFE > VS0) {
        ctx.fillStyle = '#ffffff';
        ctx.globalAlpha = 0.22; ctx.fillRect(toX(VS0), barY, toX(VFE)-toX(VS0), barH); ctx.globalAlpha = 1;
        ctx.strokeStyle = 'rgba(255,255,255,0.65)'; ctx.lineWidth = 2; ctx.strokeRect(toX(VS0), barY, toX(VFE)-toX(VS0), barH);
      }

      // Green arc (normal operating range) — from (VFE or VS0) to VNO
      const greenStart = isFinite(VFE) ? VFE : (isFinite(VS0) ? VS0 : NaN);
      if (isFinite(greenStart) && isFinite(VNO) && VNO > greenStart) {
        ctx.fillStyle = '#00d26a';
        ctx.globalAlpha = 0.20; ctx.fillRect(toX(greenStart), barY, toX(VNO)-toX(greenStart), barH); ctx.globalAlpha = 1;
        ctx.strokeStyle = 'rgba(0,210,106,0.7)'; ctx.lineWidth = 2; ctx.strokeRect(toX(greenStart), barY, toX(VNO)-toX(greenStart), barH);
      }

      // Yellow arc (caution) — VNO..VNE
      if (isFinite(VNO) && isFinite(VNE) && VNE > VNO) {
        ctx.fillStyle = '#ffaa00';
        ctx.globalAlpha = 0.20; ctx.fillRect(toX(VNO), barY, toX(VNE)-toX(VNO), barH); ctx.globalAlpha = 1;
        ctx.strokeStyle = 'rgba(255,170,0,0.75)'; ctx.lineWidth = 2; ctx.strokeRect(toX(VNO), barY, toX(VNE)-toX(VNO), barH);
      }

      // Red (never exceed) — VNE..end
      if (isFinite(VNE)) {
        ctx.fillStyle = '#ff3b3b';
        ctx.globalAlpha = 0.22; ctx.fillRect(toX(VNE), barY, barX + barW - toX(VNE), barH); ctx.globalAlpha = 1;
        ctx.strokeStyle = 'rgba(255,59,59,0.8)'; ctx.lineWidth = 2; ctx.strokeRect(toX(VNE), barY, barX + barW - toX(VNE), barH);
      }

      // Tick marks every 10 kt
      ctx.strokeStyle = 'rgba(255,255,255,0.25)'; ctx.lineWidth = 1;
      ctx.fillStyle = 'rgba(255,255,255,0.5)';
      ctx.font = '11px Consolas, monospace';
      const tickEvery = 10;
      for (let k = 0; k <= maxRef; k += tickEvery) {
        const x = toX(k);
        ctx.beginPath(); ctx.moveTo(x, barY + barH); ctx.lineTo(x, barY + barH + 6); ctx.stroke();
        if (k % 20 === 0) ctx.fillText(String(k), x - 10, barY + barH + 18);
      }

      // Current IAS pointer
      const iasX = toX(latest.iasKt);
      ctx.fillStyle = '#b7cffb';
      ctx.beginPath();
      ctx.moveTo(iasX, barY - 10); ctx.lineTo(iasX - 6, barY - 2); ctx.lineTo(iasX + 6, barY - 2); ctx.closePath();
      ctx.fill();

      // VAPP marker (target approach)
      if (isFinite(latest.VAPP)) {
        const x = toX(latest.VAPP);
        ctx.strokeStyle = 'rgba(183,207,251,0.8)'; ctx.setLineDash([4,3]);
        ctx.beginPath(); ctx.moveTo(x, barY - 8); ctx.lineTo(x, barY + barH + 8); ctx.stroke();
        ctx.setLineDash([]);
      }
    }

    function renderText() {
      iasEl.textContent = isFinite(latest.iasKt) ? Math.round(latest.iasKt).toString().padStart(2,' ') + ' kt' : '--';
      const fmt = (x) => isFinite(x) ? x.toFixed(0) : '—';
      speedsEl.innerHTML = `
        <div>VS0: <b>${fmt(latest.VS0)}</b> kt</div>
        <div>VFE: <b>${fmt(latest.VFE)}</b> kt</div>
        <div>VNO: <b>${fmt(latest.VNO)}</b> kt</div>
        <div>VNE: <b>${fmt(latest.VNE)}</b> kt</div>
        <div>VAPP: <b>${fmt(latest.VAPP)}</b> kt</div>
      `;
    }

    function updateFromFrame(frame) {
      const v = frame?.variables || {};
      // IAS: use canonical variable name
      latest.iasKt = Number(v['Aircraft.IndicatedAirspeed'] || 0) * MPS_TO_KT;
      // m/s → kt conversion helper (robust NaN handling)
      const toKt = (mps) => {
        const n = Number(mps);
        return Number.isFinite(n) ? n * MPS_TO_KT : NaN;
      };
      // Performance speeds: use canonical variable names
      latest.VS0 = toKt(v['Performance.Speed.VS0']);
      latest.VFE = toKt(v['Performance.Speed.VFE']);
      latest.VNO = toKt(v['Performance.Speed.VNO']);
      latest.VNE = toKt(v['Performance.Speed.VNE']);
      latest.VAPP = toKt(v['Performance.Speed.VAPP']);
      // sanitize NaNs
      for (const k of Object.keys(latest)) { if (!isFinite(latest[k])) latest[k] = NaN; }
      drawTape();
      renderText();
    }

    function connect(){
      try{
        setWs('connecting…');
        const ws = new WebSocket(wsUrl);
        ws.onopen = () => { setWs('connected'); if (reconnectTimer) { clearInterval(reconnectTimer); reconnectTimer = null; } };
        ws.onmessage = (ev) => { try { const frame = JSON.parse(ev.data); updateFromFrame(frame); } catch(e){} };
        ws.onclose = () => { setWs('disconnected'); if (!reconnectTimer) reconnectTimer = setInterval(connect, 3000); };
        ws.onerror = () => { setWs('error'); };
      } catch(e) {
        setWs('error'); if (!reconnectTimer) reconnectTimer = setInterval(connect, 3000);
      }
    }

    drawTape(); renderText(); connect();
  </script>
</body>
</html>
```

## Step 2: Run it
1. Start Aerofly FS4 with the Bridge running.
2. Ensure the WebSocket interface is enabled (`AEROFLY_BRIDGE_WS_ENABLE=1`).
3. Open `airspeed_envelope.html` in your browser.

## How It Works
- Receives JSON frames over WebSocket from the Bridge.
- Uses canonical keys in `variables{}` (e.g., `Performance.Speed.*`) with all 361 variables available by name.
- Converts m/s → knots and draws a simple horizontal tape with colored arcs:
  - White: VS0..VFE (flap operating range)
  - Green: VFE..VNO (normal ops)
  - Yellow: VNO..VNE (caution)
  - Red: VNE..∞ (never exceed)
- Shows VAPP as a dashed marker.

## Tips and Variations
- If some `Performance.Speed.*` values are missing in your aircraft, the tape scales around IAS and omits that arc.
- Change the canvas size to fit your layout; the scale adapts.
- Add aural alerts when IAS enters yellow/red zones.

## Troubleshooting
- Connection refused: ensure Aerofly is running and the WebSocket is enabled.
- No motion: you must be in flight; check the browser console (F12) for errors.
- Speeds look wrong: confirm the aircraft reports `Performance.Speed.*` variables; otherwise, try another aircraft.

---

🎯 Mission Complete! You built a clean, real-time web instrument that demonstrates how to consume canonical variables and render a useful flight envelope visualization.
