# Web Demos

This folder contains standalone HTML demos that connect to the Aerofly FS4 Bridge over WebSocket. Open them directly in your browser—no build tools or servers required.

Prerequisites
- Start Aerofly FS4 and ensure the Bridge is running
- Enable WebSocket interface: set `AEROFLY_BRIDGE_WS_ENABLE=1` (default port `8765`)

Included
- `altitude_monitor.html`: Minimal altitude display with color‑coded cautions (500/1000 ft thresholds).
- `copilot_checklist.html`: Co‑Pilot checklist dashboard that lights items based on canonical variables (flaps, throttle, IAS, vertical speed, on‑ground, heading).
- `airspeed_envelope.html`: Horizontal airspeed tape with performance arcs (VS0/VFE/VNO/VNE) and VAPP marker, rendered in `<canvas>`.

Tips
- If values look wrong, confirm canonical variables are enabled and you are in an active flight
- You can change thresholds and styles directly in each HTML file
