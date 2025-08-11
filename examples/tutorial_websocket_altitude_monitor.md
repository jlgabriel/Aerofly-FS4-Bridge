# Simple Tutorial #1: Real-time Altitude Monitor (WebSocket)

> **What we'll build**: A real-time web-based altitude monitor that displays your aircraft's current altitude with visual warnings when flying too low or too high.

## Prerequisites
- Aerofly FS4 with AeroflyBridge.dll installed
- Basic knowledge of HTML/JavaScript
- A web browser

## What You'll Learn
- How to connect to the WebSocket interface
- How to parse JSON flight data
- How to create a responsive real-time display

## Step 1: Create the HTML File

Create a file called `altitude_monitor.html`:

```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Altitude Monitor</title>
    <style>
        body {
            font-family: 'Courier New', monospace;
            background: #000;
            color: #0f0;
            display: flex;
            justify-content: center;
            align-items: center;
            height: 100vh;
            margin: 0;
            overflow: hidden;
        }
        .monitor {
            text-align: center;
            border: 2px solid #0f0;
            padding: 30px;
            border-radius: 10px;
            background: rgba(0, 50, 0, 0.3);
            box-shadow: 0 0 20px #0f0;
        }
        .altitude {
            font-size: 4em;
            font-weight: bold;
            margin: 20px 0;
        }
        .warning {
            background: #ff0000 !important;
            color: #fff !important;
            border-color: #ff0000 !important;
            box-shadow: 0 0 20px #ff0000 !important;
            animation: pulse 1s infinite;
        }
        .caution {
            background: #ffaa00 !important;
            color: #000 !important;
            border-color: #ffaa00 !important;
            box-shadow: 0 0 20px #ffaa00 !important;
        }
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.5; }
        }
        .status {
            margin: 10px 0;
            font-size: 1.2em;
        }
        .connected { color: #0f0; }
        .disconnected { color: #f00; }
    </style>
</head>
<body>
    <div class="monitor" id="monitor">
        <h1>ALTITUDE MONITOR</h1>
        <div class="status" id="status">Connecting...</div>
        <div class="altitude" id="altitude">---</div>
        <div id="warning-text"></div>
        <div style="font-size: 0.8em; margin-top: 20px;">
            Green: Safe altitude<br>
            Orange: Caution (below 1000ft)<br>
            Red: Danger (below 500ft)
        </div>
    </div>

    <script>
        let ws = null;
        let reconnectInterval = null;

        const monitor = document.getElementById('monitor');
        const altitudeDisplay = document.getElementById('altitude');
        const statusDisplay = document.getElementById('status');
        const warningText = document.getElementById('warning-text');

        const M_TO_FT = 3.280839895; // meters → feet

        function connect() {
            try {
                // Connect to AeroflyBridge WebSocket (default port 8765)
                ws = new WebSocket('ws://localhost:8765');
                
                ws.onopen = function() {
                    console.log('Connected to AeroflyBridge');
                    statusDisplay.textContent = 'Connected';
                    statusDisplay.className = 'status connected';
                    clearInterval(reconnectInterval);
                };

                ws.onmessage = function(event) {
                    try {
                        const data = JSON.parse(event.data);
                        updateAltitudeDisplay(data);
                    } catch (e) {
                        console.error('Error parsing JSON:', e);
                    }
                };

                ws.onclose = function() {
                    console.log('Disconnected from AeroflyBridge');
                    statusDisplay.textContent = 'Disconnected';
                    statusDisplay.className = 'status disconnected';
                    altitudeDisplay.textContent = '---';
                    resetMonitorStyle();
                    
                    // Auto-reconnect every 3 seconds
                    if (!reconnectInterval) {
                        reconnectInterval = setInterval(connect, 3000);
                    }
                };

                ws.onerror = function(error) {
                    console.error('WebSocket error:', error);
                };

            } catch (e) {
                console.error('Connection error:', e);
                if (!reconnectInterval) {
                    reconnectInterval = setInterval(connect, 3000);
                }
            }
        }

        function updateAltitudeDisplay(data) {
            // Use canonical variable map; units are meters → convert to feet for display
            const altitudeMeters = data?.variables?.["Aircraft.Altitude"] ?? 0;
            const altitudeFeet = altitudeMeters * M_TO_FT;
            
            altitudeDisplay.textContent = Math.round(altitudeFeet).toLocaleString() + ' ft';
            
            // Apply warning styles based on altitude (thresholds in feet)
            resetMonitorStyle();
            
            if (altitudeFeet < 500) {
                monitor.className = 'monitor warning';
                warningText.textContent = '⚠️ DANGER - VERY LOW ALTITUDE ⚠️';
            } else if (altitudeFeet < 1000) {
                monitor.className = 'monitor caution';
                warningText.textContent = '⚡ CAUTION - LOW ALTITUDE ⚡';
            } else {
                warningText.textContent = '✈️ Safe Flying Altitude';
            }
        }

        function resetMonitorStyle() {
            monitor.className = 'monitor';
        }

        // Start connection when page loads
        connect();
    </script>
</body>
</html>
```

## Step 2: Test the Monitor

1. **Start Aerofly FS4** with the AeroflyBridge.dll installed
2. **Open the HTML file** in your web browser
3. **Take off** and watch the altitude change in real-time!

## How It Works

### WebSocket Connection
```javascript
ws = new WebSocket('ws://localhost:8765');
```
This connects to the AeroflyBridge WebSocket server on the default port 8765.

### JSON Data Processing
```javascript
const data = JSON.parse(event.data);
const altitudeMeters = data.variables["Aircraft.Altitude"];
const altitudeFeet = altitudeMeters * 3.28084;
```
The bridge sends canonical variables in `data.variables`. We use `Aircraft.Altitude` and convert meters → feet.

### Visual Feedback System
- **Green**: Safe altitude (above 1000ft)
- **Orange**: Caution zone (500-1000ft) 
- **Red**: Danger zone (below 500ft) with pulsing animation

### Auto-Reconnection
The monitor automatically reconnects if the connection is lost, making it robust for extended flight sessions.

## Next Steps

Try modifying the code to:
- Add sound alerts for altitude warnings
- Display multiple altitude types (radar altitude, GPS altitude)
- Create altitude trend indicators (climbing/descending)
- Log altitude data to a file

## Troubleshooting

**Connection Failed?**
- Ensure Aerofly FS4 is running with AeroflyBridge.dll
- Check that WebSocket is enabled (environment variable `AEROFLY_BRIDGE_WS_ENABLE=1`)
- Verify the port matches (default 8765, configurable with `AEROFLY_BRIDGE_WS_PORT`)

**No Data Updates?**
- Start a flight in Aerofly FS4
- Make sure your aircraft is powered on
- Check browser console (F12) for any JavaScript errors

---

🎯 **Mission Complete!** You now have a working real-time altitude monitor using WebSocket connectivity. This foundation can be extended to build much more complex flight instruments and monitoring tools.