# API Reference

**Complete technical documentation for Aerofly FS4 Bridge interfaces**

Copyright (c) 2025 Juan Luis Gabriel

The bridge provides three simultaneous interfaces for accessing flight simulation data. Each interface serves different use cases while maintaining the same core functionality.

## 🚀 Quick Reference

| Interface | Best For | Access Method | Data Format |
|-----------|----------|---------------|-------------|
| **Shared Memory** | High-performance local apps | Direct memory mapping | Binary struct |
| **TCP** | Network applications | Socket connection | JSON streaming |
| **WebSocket** | Web/mobile applications | WebSocket connection | JSON bidirectional |

---

## 📡 Interface Overview

### Data Flow Architecture

```
Aerofly FS4 → Bridge DLL → Shared Memory ←→ JSON Builder
                             ↓
                    TCP/WebSocket Broadcast → External Apps
                             ↑
                    Command Processor ← JSON Commands
```

### Update Frequency
- **Default**: 50Hz (20ms intervals)
- **Configurable**: Via `AEROFLY_BRIDGE_BROADCAST_MS` environment variable
- **Range**: 5ms minimum to prevent performance issues
- **In-payload hint**: Top-level field `broadcast_rate_hz`

---

## 🧠 Shared Memory Interface

**Ultra-fast local access for performance-critical applications**

### Connection Details

| Property | Value |
|----------|-------|
| **Memory Name** | `"AeroflyBridgeData"` |
| **Access Mode** | `FILE_MAP_ALL_ACCESS` |
| **Size** | `sizeof(AeroflyBridgeData)` |
| **Thread Safety** | Mutex protected |

### C++ Integration

```cpp
#include <windows.h>

// Open existing shared memory
HANDLE hMapFile = OpenFileMapping(
    FILE_MAP_ALL_ACCESS,    // Desired access
    FALSE,                  // Inherit handle
    "AeroflyBridgeData"     // Name
);

if (hMapFile == NULL) {
    // Handle error
    return false;
}

// Map the memory view
AeroflyBridgeData* pData = (AeroflyBridgeData*)MapViewOfFile(
    hMapFile,               // Handle
    FILE_MAP_ALL_ACCESS,    // Desired access
    0, 0,                   // File offset (high, low)
    0                       // Bytes to map (0 = entire)
);

if (pData == nullptr) {
    CloseHandle(hMapFile);
    return false;
}

// Access data
double altitude = pData->aircraft_altitude;
double airspeed = pData->aircraft_indicated_airspeed;

// Access variables by canonical name (recommended)
double throttle = pData->aircraft_throttle; // Direct struct access

// Check data validity
if (pData->data_valid == 1) {
    // Data is current and valid
    uint64_t timestamp = pData->timestamp_us;
    uint32_t counter = pData->update_counter;
}

// Cleanup
UnmapViewOfFile(pData);
CloseHandle(hMapFile);
```

### Advanced: Offset Mapping for Other Languages

**The bridge automatically generates `AeroflyBridge_offsets.json` for non-C++ languages**

This file is created in the same directory as the DLL and provides memory layout information for languages that can't directly use the C++ struct.

**Generated JSON structure:**
```json
{
  "layout_version": 1,
  "array_base_offset": 16,        // Bytes from start to variables array
  "stride_bytes": 8,              // sizeof(double) = 8 bytes per variable
  "count": 358,                   // Total number of variables
  "variables": [
    {
      "index": 0,
      "name": "Aircraft.UniversalTime",
      "group": "aircraft",
      "alias": "aircraft_universal_time",
      "offset": 16                // Bytes from shared memory start
    },
    {
      "index": 1,
      "name": "Aircraft.Altitude", 
      "group": "aircraft",
      "alias": "aircraft_altitude",
      "offset": 24
    }
    // ... continues for all 358 variables
  ]
}
```

**Python integration example:**
```python
import json
import mmap
import ctypes
import struct

class AeroflyBridge:
    def __init__(self):
        # Load offset information
        with open('AeroflyBridge_offsets.json', 'r') as f:
            self.offsets = json.load(f)
        
        # Create variable lookup
        self.var_lookup = {
            v['name']: v for v in self.offsets['variables']
        }
        
        # Map shared memory
        try:
            self.shared_mem = mmap.mmap(-1, 0, "AeroflyBridgeData")
        except OSError:
            raise Exception("Bridge not running or not accessible")
    
    def get_variable(self, name):
        """Get variable by name (e.g., 'Aircraft.Altitude')"""
        if name not in self.var_lookup:
            raise KeyError(f"Variable '{name}' not found")
        
        offset = self.var_lookup[name]['offset']
        return struct.unpack('d', self.shared_mem[offset:offset+8])[0]
    
    def get_variable_by_index(self, index):
        """Get variable by index (faster)"""
        base_offset = self.offsets['array_base_offset']
        stride = self.offsets['stride_bytes']
        offset = base_offset + (index * stride)
        return struct.unpack('d', self.shared_mem[offset:offset+8])[0]
    
    def is_data_valid(self):
        """Check if data is currently valid"""
        valid_flag = struct.unpack('I', self.shared_mem[8:12])[0]
        return valid_flag == 1
    
    def get_timestamp(self):
        """Get microsecond timestamp"""
        return struct.unpack('Q', self.shared_mem[0:8])[0]

# Usage example
bridge = AeroflyBridge()

if bridge.is_data_valid():
    altitude = bridge.get_variable('Aircraft.Altitude')
    airspeed = bridge.get_variable('Aircraft.IndicatedAirspeed')
    
    # Access by canonical name (recommended)
    throttle = bridge.get_variable('Controls.Throttle')
    
    print(f"ALT: {altitude:.0f} ft, IAS: {airspeed:.1f} kts, THR: {throttle:.2f}")
```

**C# integration example:**
```csharp
using System;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Text.Json;
using System.Collections.Generic;

public class AeroflyBridge
{
    private MemoryMappedFile mmf;
    private MemoryMappedViewAccessor accessor;
    private Dictionary<string, VariableInfo> variables;
    
    public class VariableInfo
    {
        public int index { get; set; }
        public string name { get; set; }
        public string group { get; set; }
        public string alias { get; set; }
        public int offset { get; set; }
    }
    
    public class OffsetsData
    {
        public int layout_version { get; set; }
        public int array_base_offset { get; set; }
        public int stride_bytes { get; set; }
        public int count { get; set; }
        public List<VariableInfo> variables { get; set; }
    }
    
    public AeroflyBridge()
    {
        // Load offset information
        var json = File.ReadAllText("AeroflyBridge_offsets.json");
        var offsets = JsonSerializer.Deserialize<OffsetsData>(json);
        
        // Create lookup dictionary
        variables = new Dictionary<string, VariableInfo>();
        foreach (var v in offsets.variables)
        {
            variables[v.name] = v;
        }
        
        // Open shared memory
        mmf = MemoryMappedFile.OpenExisting("AeroflyBridgeData");
        accessor = mmf.CreateViewAccessor();
    }
    
    public double GetVariable(string name)
    {
        if (!variables.ContainsKey(name))
            throw new ArgumentException($"Variable '{name}' not found");
            
        var offset = variables[name].offset;
        return accessor.ReadDouble(offset);
    }
    
    public bool IsDataValid()
    {
        return accessor.ReadUInt32(8) == 1; // data_valid flag at offset 8
    }
    
    public ulong GetTimestamp()
    {
        return accessor.ReadUInt64(0); // timestamp_us at offset 0
    }
}

// Usage
var bridge = new AeroflyBridge();
if (bridge.IsDataValid())
{
    var altitude = bridge.GetVariable("Aircraft.Altitude");
    var airspeed = bridge.GetVariable("Aircraft.IndicatedAirspeed");
    Console.WriteLine($"ALT: {altitude:F0} ft, IAS: {airspeed:F1} kts");
}
```

**Benefits of offset mapping:**
- **Language agnostic**: Works with any language that supports memory mapping
- **No hardcoded values**: Automatically adapts to structure changes
- **Performance**: Direct memory access without parsing overhead
- **Future-proof**: Layout version tracking for compatibility

### Data Structure Layout

```cpp
struct AeroflyBridgeData {
    // Header (16 bytes)
    uint64_t timestamp_us;        // Microsecond timestamp
    uint32_t data_valid;          // 1 = valid, 0 = updating
    uint32_t update_counter;      // Incremental counter
    uint32_t reserved_header;     // Padding/future use
    
    // Aircraft Data (organized by system)
    double aircraft_altitude;           // Feet MSL
    double aircraft_indicated_airspeed; // Knots
    double aircraft_latitude;           // Degrees
    double aircraft_longitude;          // Degrees
    double aircraft_pitch;              // Degrees
    double aircraft_bank;               // Degrees
    // ... 358 variables total
    
    // All variables accessible by canonical names
    // No separate array needed - use struct members directly
    
    // String fields (null-terminated)
    char aircraft_name[256];
    char navigation_nav1_identifier[8];
    // ... other string fields
};
```

### Variable Index Access

```cpp
enum class VariableIndex : int {
    AIRCRAFT_ALTITUDE = 1,
    AIRCRAFT_INDICATED_AIRSPEED = 5,
    AIRCRAFT_THROTTLE = 28,
    AIRCRAFT_FLAPS = 26,
    // ... complete enumeration available
    COUNT // Total count for bounds checking
};

// Safe indexed access
if (index < (int)VariableIndex::COUNT) {
    double value = pData->aircraft_altitude; // Use named member access
}
```

### Thread Safety Notes

- **Reading**: Multiple readers safe
- **Writing**: Bridge handles all writes internally
- **Data Validity**: Always check `data_valid` flag before reading
- **Atomic Updates**: Bridge marks data invalid during updates

---

## 🌐 TCP Interface

**Network-accessible JSON API for remote monitoring and control**

### Connection Details

| Port | Purpose | Protocol | Data Format |
|------|---------|----------|-------------|
| **12345** | Data streaming | TCP | JSON (read-only) |
| **12346** | Command channel | TCP | JSON (write-only) |

### Data Stream (Port 12345)

**Continuous JSON broadcast of simulation data**

```python
import socket
import json

# Connect to data stream
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('localhost', 12345))

while True:
    # Receive JSON data (varies in size)
    data = sock.recv(16384).decode('utf-8')

    # Parse JSON (one message per line recommended)
    telemetry = json.loads(data)

    # Access canonical Aerofly variables
    altitude_m = telemetry['variables']['Aircraft.Altitude']
    ias_mps = telemetry['variables']['Aircraft.IndicatedAirspeed']

    # Access additional variables by canonical name
    throttle = telemetry['variables']['Controls.Throttle']

    # Check data validity
    if telemetry['data_valid'] == 1:
        ts_us = telemetry['timestamp']  # microseconds
        print(f"ALT: {altitude_m:.1f} m  IAS: {ias_mps:.2f} m/s  THR: {throttle:.2f}")

sock.close()
```

### Command Channel (Port 12346)

**Send control commands to the simulation**

```python
import socket
import json

# Connect to command channel
cmd_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
cmd_sock.connect(('localhost', 12346))

# Send command
command = {
    "variable": "Controls.Throttle",
    "value": 0.75
}

cmd_sock.send(json.dumps(command).encode('utf-8'))
cmd_sock.close()  # One command per connection
```

### JSON Data Schema

```json
{
  "schema": "aerofly-bridge-telemetry",
  "schema_version": 1,
  "timestamp": 1640995200000000,
  "timestamp_unit": "microseconds",
  "data_valid": 1,
  "update_counter": 12345,
  "broadcast_rate_hz": 50.0,
  "variables": {
    "Aircraft.Latitude": 47.6062,
    "Aircraft.Longitude": -122.3321,
    "Aircraft.Altitude": 1066.80,
    "Aircraft.Pitch": 0.0436,
    "Aircraft.Bank": -0.0209,
    "Aircraft.TrueHeading": 1.5708,
    "Aircraft.IndicatedAirspeed": 61.80,
    "Aircraft.GroundSpeed": 59.90,
    "Aircraft.VerticalSpeed": 0.76,
    "Aircraft.AngleOfAttack": 0.0733,
    "Aircraft.OnGround": 0,
    "Controls.Throttle": 0.75,
    "Navigation.NAV1Frequency": 108500000.0
    // ... all 358 variables with descriptive canonical names
  }
}
```

### Heading normalization

- Prefer `Aircraft.MagneticHeading` for UI/compass displays; fall back to `Aircraft.TrueHeading` if magnetic is unavailable.
- Canonical units are radians (mathematical convention, 0° = East, counter-clockwise positive). Some example/tutorial outputs may present degrees for convenience.
- To display standard aviation compass heading (0° = North, clockwise), convert mathematical degrees using:

```javascript
const src = (v["Aircraft.MagneticHeading"] ?? v["Aircraft.TrueHeading"] ?? 0);
const RAD_TO_DEG = 180 / Math.PI;
const hdgMathDeg = (Math.abs(src) <= 6.5) ? (src * RAD_TO_DEG) % 360 : (src % 360);
let heading_deg = (90 - hdgMathDeg) % 360; if (heading_deg < 0) heading_deg += 360;
```

This ensures a normalized [0, 360) compass heading consistent with cockpit instruments.

### Command Format

```json
{
  "variable": "Controls.Throttle",
  "value": 0.75
}
```

**Supported Variable Names:**
- `Controls.Throttle` (0.0 to 1.0)
- `Controls.Pitch.Input` (-1.0 to 1.0)
- `Controls.Roll.Input` (-1.0 to 1.0)
- `Controls.Yaw.Input` (-1.0 to 1.0)
- `Aircraft.Gear` (0.0 to 1.0)
- `Aircraft.Flaps` (0.0 to 1.0)
- `Navigation.COM1Frequency` (MHz)
- `Autopilot.SelectedAltitude` (feet)
- *[Complete list in Variables Reference](variables_reference.md)*

---

## 🔗 WebSocket Interface

**Modern bidirectional communication for web and mobile applications**

### Connection Details

| Property | Value |
|----------|-------|
| **Port** | `8765` (configurable) |
| **Protocol** | RFC 6455 WebSocket |
| **Subprotocol** | None |
| **Data Format** | JSON (identical to TCP) |

### JavaScript Integration

```javascript
// Connect to WebSocket
const ws = new WebSocket('ws://localhost:8765');

ws.onopen = function(event) {
    console.log('Connected to Aerofly FS4 Bridge');
};

ws.onmessage = function(event) {
    // Parse incoming telemetry data
    const data = JSON.parse(event.data);

    // Access canonical variables
    updateAltimeter(data.variables["Aircraft.Altitude"]);
    updateAirspeedIndicator(data.variables["Aircraft.IndicatedAirspeed"]);
    updateHeadingIndicator(data.variables["Aircraft.TrueHeading"]);

    // Access additional variables by canonical name
    const throttle = data.variables["Controls.Throttle"];
    updateThrottleDisplay(throttle);
};

ws.onerror = function(error) {
    console.error('WebSocket error:', error);
};

ws.onclose = function(event) {
    console.log('Connection closed');
};

// Send commands (bidirectional)
function setThrottle(value) {
    const command = {
        variable: "Controls.Throttle",
        value: value
    };
    ws.send(JSON.stringify(command));
}

// Usage
setThrottle(0.75); // Set 75% throttle
```

### Python WebSocket Client

```python
import asyncio
import websockets
import json

async def aerofly_client():
    uri = "ws://localhost:8765"
    
    try:
        async with websockets.connect(uri) as websocket:
            print("Connected to Aerofly FS4 Bridge")
            
            while True:
                # Receive data
                message = await websocket.recv()
                data = json.loads(message)
                
                # Process telemetry
                altitude_m = data['variables']['Aircraft.Altitude']
                ias_mps = data['variables']['Aircraft.IndicatedAirspeed']
                throttle = data['variables']['Controls.Throttle']
                print(f"ALT: {altitude_m:8.1f} m  IAS: {ias_mps:6.2f} m/s  THR: {throttle:.2f}")
                
                # Send commands conditionally
                if altitude < 1000:  # Increase throttle at low altitude
                    command = {
                        "variable": "Controls.Throttle",
                        "value": 0.9
                    }
                    await websocket.send(json.dumps(command))
    
    except websockets.exceptions.ConnectionClosed:
        print("Connection closed")
    except Exception as e:
        print(f"Error: {e}")

# Run the client
asyncio.run(aerofly_client())
```

### Mobile App Integration (React Native)

```javascript
import React, { useState, useEffect } from 'react';

const AeroflyTelemetry = () => {
    const [flightData, setFlightData] = useState(null);
    const [ws, setWs] = useState(null);

    useEffect(() => {
        // Connect to bridge
        const websocket = new WebSocket('ws://192.168.1.100:8765');
        
        websocket.onmessage = (event) => {
            const data = JSON.parse(event.data);
            setFlightData(data);
        };
        
        setWs(websocket);
        
    return () => websocket.close();
    }, []);

    const sendCommand = (variable, value) => {
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({ variable, value }));
        }
    };

    return (
        <View>
            <Text>Altitude: {flightData?.variables?.["Aircraft.Altitude"] ?? 0} m</Text>
            <Text>Airspeed: {flightData?.variables?.["Aircraft.IndicatedAirspeed"] ?? 0} m/s</Text>
            <Text>Throttle: {flightData?.variables?.["Controls.Throttle"] ?? 0}</Text>
            
            <Button 
                title="Set Flaps 10°" 
                onPress={() => sendCommand("Aircraft.Flaps", 0.33)} 
            />
        </View>
    );
};
```

---

## ⚙️ Configuration

### Environment Variables

Configure the bridge behavior using Windows environment variables:

| Variable | Default | Description | Range |
|----------|---------|-------------|-------|
| `AEROFLY_BRIDGE_WS_ENABLE` | `1` | Enable WebSocket server | `0` or `1` |
| `AEROFLY_BRIDGE_WS_PORT` | `8765` | WebSocket server port | `1024-65535` |
| `AEROFLY_BRIDGE_BROADCAST_MS` | `20` | Telemetry broadcast interval | `5-1000` ms |

### Setting Environment Variables (Windows)

**Method 1: System Properties**
1. Right-click "This PC" → Properties
2. Advanced system settings → Environment Variables
3. Add new User variable

**Method 2: Command Line**
```cmd
set AEROFLY_BRIDGE_WS_PORT=9000
set AEROFLY_BRIDGE_BROADCAST_MS=50
```

**Method 3: PowerShell**
```powershell
[Environment]::SetEnvironmentVariable("AEROFLY_BRIDGE_WS_PORT", "9000", "User")
```

### Performance Tuning

**High-frequency data (trading latency for data rate):**
```cmd
set AEROFLY_BRIDGE_BROADCAST_MS=5  # 200Hz updates
```

**Low-bandwidth scenarios:**
```cmd
set AEROFLY_BRIDGE_BROADCAST_MS=100  # 10Hz updates
```

**Disable WebSocket (TCP only):**
```cmd
set AEROFLY_BRIDGE_WS_ENABLE=0
```

---

## 🔒 Security Considerations

### Network Access

- **Localhost Only**: Default configuration binds to localhost
- **No Authentication**: Direct socket access (design for trusted networks)
- **Firewall**: Windows Firewall may block initial connections

### Production Deployments

**For public network access:**

1. **Firewall Rules**: Configure Windows Firewall
   ```cmd
   netsh advfirewall firewall add rule name="Aerofly Bridge TCP" dir=in action=allow protocol=TCP localport=12345
   netsh advfirewall firewall add rule name="Aerofly Bridge WS" dir=in action=allow protocol=TCP localport=8765
   ```

2. **Binding Configuration**: Currently localhost-only
   - Future versions may support configurable binding
   - Use reverse proxy (nginx) for external access

3. **Rate Limiting**: No built-in rate limiting
   - Monitor connection count via `GetClientCount()`
   - Implement application-level throttling

---

## 📊 Performance Characteristics

### Shared Memory
- **Latency**: ~1-5 microseconds
- **Throughput**: Limited by memory bandwidth
- **CPU Usage**: Minimal (direct memory access)
- **Scalability**: Multiple readers, single writer

### TCP Interface
- **Latency**: ~100-500 microseconds (local)
- **Throughput**: ~1-10 MB/s typical
- **CPU Usage**: Low to moderate
- **Concurrent Clients**: 50+ typical (OS dependent)

### WebSocket Interface
- **Latency**: ~200-800 microseconds (local)
- **Throughput**: ~1-5 MB/s typical
- **CPU Usage**: Moderate (JSON + WebSocket overhead)
- **Concurrent Clients**: 20+ typical

### Memory Usage
- **Shared Memory**: ~50KB (data structure)
- **Bridge DLL**: ~2-5MB (loaded in Aerofly process)
- **Per TCP Client**: ~64KB buffers
- **Per WebSocket Client**: ~128KB buffers

---

## 🐛 Error Handling

### Connection Errors

**Shared Memory:**
```cpp
HANDLE hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, "AeroflyBridgeData");
if (hMapFile == NULL) {
    DWORD error = GetLastError();
    switch (error) {
        case ERROR_FILE_NOT_FOUND:
            // Bridge not running or not loaded
            break;
        case ERROR_ACCESS_DENIED:
            // Permission issue
            break;
    }
}
```

**TCP/WebSocket:**
```python
import socket
import errno

try:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(('localhost', 12345))
except socket.error as e:
    if e.errno == errno.ECONNREFUSED:
        print("Bridge not running")
    elif e.errno == errno.ETIMEDOUT:
        print("Connection timeout")
```

### Data Validation

```cpp
// Check data validity
if (pData->data_valid != 1) {
    // Data is being updated, wait or skip this frame
    return;
}

// Check for reasonable values
if (pData->aircraft_altitude < -1000 || pData->aircraft_altitude > 100000) {
    // Suspicious data, possibly corrupted
    return;
}

// Check update frequency
uint32_t expected_counter = last_counter + 1;
if (pData->update_counter != expected_counter) {
    // Missed updates or counter wrapped
}
```

### JSON Parsing Errors

```javascript
ws.onmessage = function(event) {
    try {
        const data = JSON.parse(event.data);
        
        // Validate expected fields
        if (typeof data.aircraft?.altitude !== 'number') {
            console.warn('Invalid altitude data');
            return;
        }
        
        processFlightData(data);
    } catch (error) {
        console.error('JSON parse error:', error);
    }
};
```

---

## 🧪 Testing & Debugging

### Verify Interface Availability

**Check TCP ports:**
```cmd
netstat -an | findstr :12345
netstat -an | findstr :8765
```

**Test basic connectivity:**
```cmd
telnet localhost 12345
```

### Debug Data Flow

**Monitor shared memory:**
```cpp
while (true) {
    if (pData->data_valid == 1) {
        printf("Counter: %u, Altitude: %.1f\n", 
               pData->update_counter, 
               pData->aircraft_altitude);
    }
    Sleep(100);
}
```

**WebSocket test page:**
```html
<!DOCTYPE html>
<html>
<head><title>Aerofly Bridge Test</title></head>
<body>
    <div id="data"></div>
    <script>
        const ws = new WebSocket('ws://localhost:8765');
        ws.onmessage = (e) => {
            const data = JSON.parse(e.data);
            document.getElementById('data').innerHTML = 
                `ALT: ${data.variables["Aircraft.Altitude"]} m<br>
                 IAS: ${data.variables["Aircraft.IndicatedAirspeed"]} m/s<br>
                 THR: ${data.variables["Controls.Throttle"]}`;
        };
    </script>
</body>
</html>
```

---

<!-- Removed "Next Steps" cross-doc links to avoid broken references in public repo. -->