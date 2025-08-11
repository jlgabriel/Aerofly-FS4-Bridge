# Simple Tutorial #4: Virtual Autopilot Controller (WebSocket Commands)

> **What we'll build**: A web-based autopilot controller that can both read flight data AND send commands to control the aircraft, featuring altitude hold, heading hold, and speed control with a touch-friendly interface.

## Prerequisites
- Aerofly FS4 with AeroflyBridge.dll installed
- Basic knowledge of HTML/JavaScript
- A web browser (mobile-friendly design included)

## What You'll Learn
- How to send commands via bidirectional WebSocket
- How to build interactive flight controls
- How to create feedback loops for autopilot functions
- How to design touch-friendly aviation interfaces

## Step 1: Create the Autopilot Controller

Create a file called `autopilot_controller.html`:

```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Virtual Autopilot Controller</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: 'Arial', sans-serif;
            background: linear-gradient(135deg, #1e3c72, #2a5298);
            color: white;
            min-height: 100vh;
            padding: 10px;
            user-select: none;
        }
        
        .cockpit {
            max-width: 800px;
            margin: 0 auto;
            background: rgba(0, 0, 0, 0.8);
            border-radius: 15px;
            padding: 20px;
            box-shadow: 0 10px 30px rgba(0, 0, 0, 0.5);
        }
        
        .status-bar {
            display: flex;
            justify-content: space-between;
            align-items: center;
            background: rgba(0, 50, 100, 0.6);
            padding: 10px 15px;
            border-radius: 8px;
            margin-bottom: 20px;
            font-size: 14px;
        }
        
        .connected { color: #00ff00; }
        .disconnected { color: #ff4444; }
        
        .flight-display {
            display: grid;
            grid-template-columns: 1fr 1fr 1fr;
            gap: 15px;
            margin-bottom: 25px;
        }
        
        .display-item {
            background: rgba(0, 100, 200, 0.3);
            padding: 15px;
            border-radius: 8px;
            text-align: center;
            border: 2px solid rgba(0, 150, 255, 0.5);
        }
        
        .display-label {
            font-size: 12px;
            color: #aaa;
            margin-bottom: 5px;
        }
        
        .display-value {
            font-size: 24px;
            font-weight: bold;
            font-family: 'Courier New', monospace;
        }
        
        .autopilot-panel {
            display: grid;
            grid-template-columns: 1fr 1fr 1fr;
            gap: 20px;
            margin-bottom: 20px;
        }
        
        .ap-control {
            background: rgba(50, 50, 50, 0.8);
            padding: 20px;
            border-radius: 12px;
            border: 2px solid #333;
            text-align: center;
        }
        
        .ap-control.active {
            border-color: #00ff00;
            background: rgba(0, 100, 0, 0.3);
        }
        
        .ap-title {
            font-size: 16px;
            margin-bottom: 15px;
            color: #fff;
        }
        
        .ap-button {
            background: linear-gradient(145deg, #4a4a4a, #2a2a2a);
            border: none;
            color: white;
            padding: 12px 20px;
            border-radius: 8px;
            font-size: 14px;
            font-weight: bold;
            cursor: pointer;
            transition: all 0.2s;
            margin: 5px;
            min-width: 80px;
            touch-action: manipulation;
        }
        
        .ap-button:hover {
            background: linear-gradient(145deg, #5a5a5a, #3a3a3a);
            transform: translateY(-2px);
        }
        
        .ap-button:active {
            transform: translateY(0);
        }
        
        .ap-button.active {
            background: linear-gradient(145deg, #00aa00, #006600);
            box-shadow: 0 0 10px rgba(0, 255, 0, 0.5);
        }
        
        .ap-input {
            background: rgba(0, 0, 0, 0.7);
            border: 2px solid #555;
            color: white;
            padding: 10px;
            border-radius: 6px;
            font-size: 18px;
            text-align: center;
            width: 100%;
            margin: 10px 0;
            font-family: 'Courier New', monospace;
        }
        
        .ap-input:focus {
            border-color: #00aaff;
            outline: none;
        }
        
        .increment-buttons {
            display: flex;
            justify-content: space-between;
            margin-top: 10px;
        }
        
        .inc-btn {
            background: #333;
            border: 1px solid #555;
            color: white;
            padding: 8px 12px;
            border-radius: 4px;
            cursor: pointer;
            font-size: 12px;
            touch-action: manipulation;
        }
        
        .inc-btn:hover {
            background: #444;
        }
        
        .command-log {
            background: rgba(0, 0, 0, 0.6);
            border-radius: 8px;
            padding: 15px;
            height: 150px;
            overflow-y: auto;
            font-family: 'Courier New', monospace;
            font-size: 12px;
            border: 1px solid #333;
        }
        
        .log-entry {
            margin-bottom: 5px;
            padding: 2px 5px;
            border-radius: 3px;
        }
        
        .log-sent { background: rgba(0, 100, 200, 0.3); }
        .log-received { background: rgba(0, 150, 0, 0.3); }
        .log-error { background: rgba(200, 0, 0, 0.3); }
        
        @media (max-width: 768px) {
            .flight-display,
            .autopilot-panel {
                grid-template-columns: 1fr;
            }
            
            .ap-button {
                padding: 15px 25px;
                font-size: 16px;
            }
        }
    </style>
</head>
<body>
    <div class="cockpit">
        <div class="status-bar">
            <div>
                <span id="connection-status" class="disconnected">Disconnected</span>
                | <span id="client-info">Virtual Autopilot v1.0</span>
            </div>
            <div id="data-rate">0 Hz</div>
        </div>
        
        <div class="flight-display">
            <div class="display-item">
                <div class="display-label">ALTITUDE</div>
                <div class="display-value" id="current-altitude">---</div>
            </div>
            <div class="display-item">
                <div class="display-label">HEADING</div>
                <div class="display-value" id="current-heading">---</div>
            </div>
            <div class="display-item">
                <div class="display-label">AIRSPEED</div>
                <div class="display-value" id="current-speed">---</div>
            </div>
        </div>
        
        <div class="autopilot-panel">
            <!-- Altitude Hold -->
            <div class="ap-control" id="alt-control">
                <div class="ap-title">ALTITUDE HOLD</div>
                <button class="ap-button" id="alt-btn" onclick="toggleAltitudeHold()">OFF</button>
                <input type="number" class="ap-input" id="target-altitude" value="3000" min="0" max="50000" step="100">
                <div class="increment-buttons">
                    <button class="inc-btn" onclick="adjustAltitude(-1000)">-1000</button>
                    <button class="inc-btn" onclick="adjustAltitude(-100)">-100</button>
                    <button class="inc-btn" onclick="adjustAltitude(100)">+100</button>
                    <button class="inc-btn" onclick="adjustAltitude(1000)">+1000</button>
                </div>
            </div>
            
            <!-- Heading Hold -->
            <div class="ap-control" id="hdg-control">
                <div class="ap-title">HEADING HOLD</div>
                <button class="ap-button" id="hdg-btn" onclick="toggleHeadingHold()">OFF</button>
                <input type="number" class="ap-input" id="target-heading" value="360" min="1" max="360" step="1">
                <div class="increment-buttons">
                    <button class="inc-btn" onclick="adjustHeading(-45)">-45°</button>
                    <button class="inc-btn" onclick="adjustHeading(-10)">-10°</button>
                    <button class="inc-btn" onclick="adjustHeading(10)">+10°</button>
                    <button class="inc-btn" onclick="adjustHeading(45)">+45°</button>
                </div>
            </div>
            
            <!-- Speed Hold -->
            <div class="ap-control" id="spd-control">
                <div class="ap-title">SPEED HOLD</div>
                <button class="ap-button" id="spd-btn" onclick="toggleSpeedHold()">OFF</button>
                <input type="number" class="ap-input" id="target-speed" value="120" min="50" max="300" step="5">
                <div class="increment-buttons">
                    <button class="inc-btn" onclick="adjustSpeed(-20)">-20</button>
                    <button class="inc-btn" onclick="adjustSpeed(-5)">-5</button>
                    <button class="inc-btn" onclick="adjustSpeed(5)">+5</button>
                    <button class="inc-btn" onclick="adjustSpeed(20)">+20</button>
                </div>
            </div>
        </div>
        
        <div class="command-log" id="command-log">
            <div class="log-entry">Virtual Autopilot Controller Ready</div>
            <div class="log-entry">Connecting to AeroflyBridge WebSocket...</div>
        </div>
    </div>

    <script>
        class VirtualAutopilot {
            constructor() {
                this.ws = null;
                this.reconnectInterval = null;
                this.dataUpdateCount = 0;
                this.lastDataUpdate = Date.now();
                
                // Autopilot states
                this.altitudeHold = false;
                this.headingHold = false;
                this.speedHold = false;
                
                // Current aircraft data
                this.currentData = {
                    altitude: 0,
                    heading: 0,
                    speed: 0
                };
                
                // Control parameters
                this.controlGains = {
                    altitude: 0.1,
                    heading: 0.2,
                    speed: 0.05
                };
                
                // Start connection
                this.connect();
                
                // Start control loop
                setInterval(() => this.controlLoop(), 100); // 10 Hz control
                setInterval(() => this.updateDataRate(), 1000); // 1 Hz rate display
            }
            
            connect() {
                try {
                    this.ws = new WebSocket('ws://localhost:8765');
                    
                    this.ws.onopen = () => {
                        this.log('Connected to AeroflyBridge', 'received');
                        document.getElementById('connection-status').textContent = 'Connected';
                        document.getElementById('connection-status').className = 'connected';
                        clearInterval(this.reconnectInterval);
                    };
                    
                    this.ws.onmessage = (event) => {
                        try {
                            const data = JSON.parse(event.data);
                            this.processFlightData(data);
                            this.dataUpdateCount++;
                        } catch (e) {
                            this.log(`JSON Parse Error: ${e.message}`, 'error');
                        }
                    };
                    
                    this.ws.onclose = () => {
                        this.log('Disconnected from AeroflyBridge', 'error');
                        document.getElementById('connection-status').textContent = 'Disconnected';
                        document.getElementById('connection-status').className = 'disconnected';
                        
                        // Auto-reconnect
                        if (!this.reconnectInterval) {
                            this.reconnectInterval = setInterval(() => this.connect(), 3000);
                        }
                    };
                    
                    this.ws.onerror = (error) => {
                        this.log(`WebSocket Error: ${error}`, 'error');
                    };
                    
                } catch (e) {
                    this.log(`Connection Error: ${e.message}`, 'error');
                    if (!this.reconnectInterval) {
                        this.reconnectInterval = setInterval(() => this.connect(), 3000);
                    }
                }
            }
            
            processFlightData(data) {
                // Extract current flight parameters
                const flightData = data.flight_data || {};
                
                this.currentData.altitude = flightData.BarometricAltitude || 0;
                this.currentData.heading = flightData.TrueHeading || 0;
                this.currentData.speed = flightData.IndicatedAirspeed || 0;
                
                // Update display
                document.getElementById('current-altitude').textContent = Math.round(this.currentData.altitude);
                document.getElementById('current-heading').textContent = Math.round(this.currentData.heading) + '°';
                document.getElementById('current-speed').textContent = Math.round(this.currentData.speed);
            }
            
            sendCommand(command) {
                if (this.ws && this.ws.readyState === WebSocket.OPEN) {
                    const commandStr = JSON.stringify(command);
                    this.ws.send(commandStr);
                    this.log(`SENT: ${commandStr}`, 'sent');
                    return true;
                } else {
                    this.log('Cannot send command - not connected', 'error');
                    return false;
                }
            }
            
            controlLoop() {
                if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return;
                
                // Altitude Hold Control
                if (this.altitudeHold) {
                    const targetAlt = parseFloat(document.getElementById('target-altitude').value);
                    const altError = targetAlt - this.currentData.altitude;
                    
                    if (Math.abs(altError) > 50) { // 50 ft deadband
                        const elevatorInput = Math.max(-1, Math.min(1, altError * this.controlGains.altitude));
                        this.sendCommand({
                            variable: "ElevatorInput",
                            value: elevatorInput
                        });
                    }
                }
                
                // Heading Hold Control
                if (this.headingHold) {
                    const targetHdg = parseFloat(document.getElementById('target-heading').value);
                    let hdgError = targetHdg - this.currentData.heading;
                    
                    // Handle heading wrap-around
                    if (hdgError > 180) hdgError -= 360;
                    if (hdgError < -180) hdgError += 360;
                    
                    if (Math.abs(hdgError) > 2) { // 2 degree deadband
                        const aileronInput = Math.max(-1, Math.min(1, hdgError * this.controlGains.heading));
                        this.sendCommand({
                            variable: "AileronInput",
                            value: aileronInput
                        });
                    }
                }
                
                // Speed Hold Control
                if (this.speedHold) {
                    const targetSpd = parseFloat(document.getElementById('target-speed').value);
                    const spdError = targetSpd - this.currentData.speed;
                    
                    if (Math.abs(spdError) > 5) { // 5 kt deadband
                        const throttleInput = Math.max(0, Math.min(1, 0.5 + (spdError * this.controlGains.speed)));
                        this.sendCommand({
                            variable: "ThrottleInput",
                            value: throttleInput
                        });
                    }
                }
            }
            
            updateDataRate() {
                const now = Date.now();
                const elapsed = (now - this.lastDataUpdate) / 1000;
                const rate = Math.round(this.dataUpdateCount / elapsed);
                
                document.getElementById('data-rate').textContent = `${rate} Hz`;
                
                this.dataUpdateCount = 0;
                this.lastDataUpdate = now;
            }
            
            log(message, type = 'info') {
                const logContainer = document.getElementById('command-log');
                const entry = document.createElement('div');
                entry.className = `log-entry log-${type}`;
                entry.textContent = `${new Date().toLocaleTimeString()}: ${message}`;
                
                logContainer.appendChild(entry);
                logContainer.scrollTop = logContainer.scrollHeight;
                
                // Keep only last 50 entries
                while (logContainer.children.length > 50) {
                    logContainer.removeChild(logContainer.firstChild);
                }
            }
        }
        
        // Initialize autopilot controller
        const autopilot = new VirtualAutopilot();
        
        // Autopilot Control Functions
        function toggleAltitudeHold() {
            autopilot.altitudeHold = !autopilot.altitudeHold;
            const btn = document.getElementById('alt-btn');
            const control = document.getElementById('alt-control');
            
            if (autopilot.altitudeHold) {
                btn.textContent = 'ON';
                btn.classList.add('active');
                control.classList.add('active');
                autopilot.log('Altitude Hold ENGAGED', 'received');
                
                // Set target to current altitude
                document.getElementById('target-altitude').value = Math.round(autopilot.currentData.altitude);
            } else {
                btn.textContent = 'OFF';
                btn.classList.remove('active');
                control.classList.remove('active');
                autopilot.log('Altitude Hold DISENGAGED', 'sent');
            }
        }
        
        function toggleHeadingHold() {
            autopilot.headingHold = !autopilot.headingHold;
            const btn = document.getElementById('hdg-btn');
            const control = document.getElementById('hdg-control');
            
            if (autopilot.headingHold) {
                btn.textContent = 'ON';
                btn.classList.add('active');
                control.classList.add('active');
                autopilot.log('Heading Hold ENGAGED', 'received');
                
                // Set target to current heading
                document.getElementById('target-heading').value = Math.round(autopilot.currentData.heading);
            } else {
                btn.textContent = 'OFF';
                btn.classList.remove('active');
                control.classList.remove('active');
                autopilot.log('Heading Hold DISENGAGED', 'sent');
            }
        }
        
        function toggleSpeedHold() {
            autopilot.speedHold = !autopilot.speedHold;
            const btn = document.getElementById('spd-btn');
            const control = document.getElementById('spd-control');
            
            if (autopilot.speedHold) {
                btn.textContent = 'ON';
                btn.classList.add('active');
                control.classList.add('active');
                autopilot.log('Speed Hold ENGAGED', 'received');
                
                // Set target to current speed
                document.getElementById('target-speed').value = Math.round(autopilot.currentData.speed);
            } else {
                btn.textContent = 'OFF';
                btn.classList.remove('active');
                control.classList.remove('active');
                autopilot.log('Speed Hold DISENGAGED', 'sent');
            }
        }
        
        // Increment/Decrement Functions
        function adjustAltitude(change) {
            const input = document.getElementById('target-altitude');
            input.value = Math.max(0, Math.min(50000, parseInt(input.value) + change));
        }
        
        function adjustHeading(change) {
            const input = document.getElementById('target-heading');
            let newValue = parseInt(input.value) + change;
            
            // Handle wrap-around
            if (newValue > 360) newValue -= 360;
            if (newValue < 1) newValue += 360;
            
            input.value = newValue;
        }
        
        function adjustSpeed(change) {
            const input = document.getElementById('target-speed');
            input.value = Math.max(50, Math.min(300, parseInt(input.value) + change));
        }
    </script>
</body>
</html>
```

## Step 2: Test the Autopilot

1. **Start Aerofly FS4** with AeroflyBridge.dll installed
2. **Take off** and reach a stable flight condition
3. **Open the HTML file** in your browser
4. **Engage autopilot modes** and watch the aircraft respond!

## How It Works

### Bidirectional WebSocket Communication
```javascript
// Receiving flight data
this.ws.onmessage = (event) => {
    const data = JSON.parse(event.data);
    this.processFlightData(data);
};

// Sending control commands
this.ws.send(JSON.stringify({
    variable: "ElevatorInput",
    value: elevatorInput
}));
```

### Control Loop Implementation
```javascript
controlLoop() {
    // Altitude Hold: P-controller
    const altError = targetAlt - this.currentData.altitude;
    const elevatorInput = altError * this.controlGains.altitude;
    
    // Send control command
    this.sendCommand({
        variable: "ElevatorInput", 
        value: elevatorInput
    });
}
```

### Autopilot Modes

#### 1. Altitude Hold
- **Engages**: Controls elevator to maintain target altitude
- **Deadband**: ±50 feet to prevent oscillation
- **P-Controller**: Proportional response to altitude error

#### 2. Heading Hold
- **Engages**: Controls ailerons to maintain target heading
- **Deadband**: ±2 degrees
- **Wrap-around**: Handles 359°→1° transitions correctly

#### 3. Speed Hold
- **Engages**: Controls throttle to maintain target airspeed
- **Deadband**: ±5 knots
- **Range**: 0-100% throttle with bias

### Touch-Friendly Interface
- Large buttons optimized for mobile devices
- Increment/decrement controls for precise adjustments
- Visual feedback with active state indicators
- Responsive grid layout

## Step 3: Understanding the Command Protocol

### Command Format
```javascript
{
    "variable": "VariableName",
    "value": numericValue
}
```

### Available Control Variables
- `ElevatorInput` (-1.0 to 1.0) - Pitch control
- `AileronInput` (-1.0 to 1.0) - Roll control  
- `RudderInput` (-1.0 to 1.0) - Yaw control
- `ThrottleInput` (0.0 to 1.0) - Engine power

## Step 4: Safety Features

### Built-in Protections
```javascript
// Control surface limits
const elevatorInput = Math.max(-1, Math.min(1, altError * gain));

// Deadbands prevent oscillation
if (Math.abs(altError) > 50) { /* apply control */ }

// Automatic disengagement on manual input
```

### Emergency Procedures
- **Manual Override**: Any manual control input disengages autopilot
- **Connection Loss**: Controls revert to manual if WebSocket disconnects
- **Range Limits**: All inputs are clamped to safe ranges

## Next Steps

Enhance the autopilot with:
- **Navigation Mode**: Follow GPS waypoints
- **ILS Approach**: Automated instrument landing
- **Vertical Speed Mode**: Climb/descent rate control
- **Flight Level Changes**: Automated altitude changes
- **Wind Compensation**: Account for crosswinds in heading hold

## Troubleshooting

**Commands Not Working?**
- Verify WebSocket connection is bidirectional
- Check that commands are formatted as valid JSON
- Ensure aircraft is in flight (some controls inactive on ground)

**Autopilot Oscillating?**
- Increase deadband values
- Reduce control gain values
- Check for control input conflicts

**Mobile Interface Issues?**
- Enable touch-action manipulation
- Use larger button targets
- Test in mobile browser developer mode

---

🎯 **Mission Complete!** You now have a fully functional virtual autopilot that demonstrates bidirectional communication with AeroflyBridge. This showcases how external applications can both monitor AND control the aircraft, opening possibilities for advanced flight management systems, training tools, and automated flight operations.