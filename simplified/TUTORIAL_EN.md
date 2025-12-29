# Tutorial: Using the AeroflyReader DLL

This tutorial will guide you step by step to connect your projects with Aerofly FS4 using the simplified DLL. It's designed for hobbyists and flight simulation enthusiasts - you don't need to be a professional programmer.

## Table of Contents

1. [Installing the DLL](#1-installing-the-dll)
2. [Understanding the two connection methods](#2-understanding-the-two-connection-methods)
3. [TCP Connection (the easiest)](#3-tcp-connection-the-easiest)
4. [Shared Memory Connection (the fastest)](#4-shared-memory-connection-the-fastest)
5. [Unit Conversion](#5-unit-conversion)
6. [Complete Practical Examples](#6-complete-practical-examples)
7. [Troubleshooting](#7-troubleshooting)

---

## 1. Installing the DLL

### Step 1: Download or build the DLL

There's a pre-built DLL ready to use in the `bin/` folder:

```
simplified/bin/AeroflyReader.dll
```

### Step 2: Copy the DLL to the Aerofly folder

Open PowerShell and run:

```powershell
copy simplified\bin\AeroflyReader.dll "$env:USERPROFILE\Documents\Aerofly FS 4\external_dll\"
```

Or do it manually:
1. Go to `Documents\Aerofly FS 4\`
2. Create the `external_dll` folder if it doesn't exist
3. Copy `AeroflyReader.dll` into that folder

### Step 3: Start Aerofly FS4

The DLL loads automatically when the simulator starts. No additional configuration needed.

---

## 2. Understanding the two connection methods

The DLL offers two methods to get data:

| Method | Description | When to use it |
|--------|-------------|----------------|
| **TCP** | Receives data as JSON text over network | Easiest to use. Ideal for getting started. |
| **Shared Memory** | Reads directly from memory | Fastest. Ideal for high-performance applications. |

**Recommendation:** If you're just starting, use TCP. It's simpler and works perfectly for most projects.

---

## 3. TCP Connection (the easiest)

### What is TCP?

TCP is like a "phone call" between programs. Your program calls port 12345, the DLL answers, and sends you data continuously in JSON format (structured text that's easy to read).

### Minimal Python Example

```python
import socket
import json

# 1. Connect to the DLL
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('localhost', 12345))

# 2. Receive and display data
buffer = ""
while True:
    buffer += sock.recv(4096).decode('utf-8')

    # Look for complete lines (ending with \n)
    while '\n' in buffer:
        line, buffer = buffer.split('\n', 1)
        if line.strip():
            data = json.loads(line)

            # Display some data
            print(f"Altitude: {data['altitude']:.0f} ft")
            print(f"Speed: {data['indicated_airspeed'] * 1.94384:.0f} knots")
```

### Minimal C# Example

```csharp
using System;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;

class Program
{
    static void Main()
    {
        // 1. Connect to the DLL
        TcpClient client = new TcpClient("localhost", 12345);
        NetworkStream stream = client.GetStream();

        byte[] buffer = new byte[4096];
        string accumulated = "";

        // 2. Receive data continuously
        while (true)
        {
            int bytesRead = stream.Read(buffer, 0, buffer.Length);
            accumulated += Encoding.UTF8.GetString(buffer, 0, bytesRead);

            // Process complete lines
            while (accumulated.Contains("\n"))
            {
                int index = accumulated.IndexOf('\n');
                string line = accumulated.Substring(0, index);
                accumulated = accumulated.Substring(index + 1);

                if (!string.IsNullOrWhiteSpace(line))
                {
                    var data = JsonSerializer.Deserialize<JsonElement>(line);
                    double altitude = data.GetProperty("altitude").GetDouble();
                    Console.WriteLine($"Altitude: {altitude:F0} ft");
                }
            }
        }
    }
}
```

### Minimal JavaScript/Node.js Example

```javascript
const net = require('net');

// 1. Connect to the DLL
const client = new net.Socket();
client.connect(12345, 'localhost', () => {
    console.log('Connected to Aerofly!');
});

// 2. Receive data
let buffer = '';
client.on('data', (data) => {
    buffer += data.toString();

    // Process complete lines
    while (buffer.includes('\n')) {
        const index = buffer.indexOf('\n');
        const line = buffer.substring(0, index);
        buffer = buffer.substring(index + 1);

        if (line.trim()) {
            const flightData = JSON.parse(line);
            console.log(`Altitude: ${flightData.altitude.toFixed(0)} ft`);
        }
    }
});
```

### Arduino Example (ESP32/ESP8266 with WiFi)

```cpp
#include <WiFi.h>
#include <ArduinoJson.h>

const char* ssid = "YOUR_WIFI";
const char* password = "YOUR_PASSWORD";
const char* host = "192.168.1.100";  // Your PC's IP
const int port = 12345;

WiFiClient client;
String buffer = "";

void setup() {
    Serial.begin(115200);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected!");

    // Connect to the DLL
    if (client.connect(host, port)) {
        Serial.println("Connected to Aerofly!");
    }
}

void loop() {
    while (client.available()) {
        char c = client.read();
        buffer += c;

        if (c == '\n') {
            // Parse JSON
            StaticJsonDocument<2048> doc;
            if (deserializeJson(doc, buffer) == DeserializationOk) {
                double altitude = doc["altitude"];
                double speed = doc["indicated_airspeed"];

                Serial.printf("Alt: %.0f ft, IAS: %.0f kts\n",
                              altitude, speed * 1.94384);
            }
            buffer = "";
        }
    }

    // Reconnect if connection lost
    if (!client.connected()) {
        client.connect(host, port);
    }
    delay(10);
}
```

---

## 4. Shared Memory Connection (the fastest)

### What is Shared Memory?

It's a memory zone shared between Aerofly and your program. Since they read from the same location, it's extremely fast (microseconds).

**Note:** Only works on the same computer where Aerofly is running.

### Data Structure in Memory

The data is organized like this (in order):

| Offset | Type | Size | Field |
|--------|------|------|-------|
| 0 | uint64 | 8 bytes | timestamp_us |
| 8 | uint32 | 4 bytes | data_valid (1=valid, 0=no) |
| 12 | uint32 | 4 bytes | update_counter |
| 16 | double | 8 bytes | latitude (radians) |
| 24 | double | 8 bytes | longitude (radians) |
| 32 | double | 8 bytes | altitude (feet) |
| 40 | double | 8 bytes | height (feet AGL) |
| 48 | double | 8 bytes | pitch (radians) |
| 56 | double | 8 bytes | bank (radians) |
| 64 | double | 8 bytes | true_heading (radians) |
| 72 | double | 8 bytes | magnetic_heading (radians) |
| 80 | double | 8 bytes | indicated_airspeed (m/s) |
| 88 | double | 8 bytes | ground_speed (m/s) |
| 96 | double | 8 bytes | vertical_speed (m/s) |
| ... | ... | ... | (continues) |

### Complete Python Example

```python
import mmap
import struct
import time
import math

# Shared memory name
SHM_NAME = "AeroflyReaderData"
SHM_SIZE = 1024  # bytes

def main():
    print("Connecting to Shared Memory...")

    try:
        # Open shared memory
        shm = mmap.mmap(-1, SHM_SIZE, SHM_NAME, access=mmap.ACCESS_READ)
        print("Connected!")
    except Exception as e:
        print(f"Error: {e}")
        print("Make sure Aerofly is running with the DLL loaded.")
        return

    try:
        while True:
            # Go back to the beginning
            shm.seek(0)

            # Read header (16 bytes)
            timestamp = struct.unpack('Q', shm.read(8))[0]  # uint64
            data_valid = struct.unpack('I', shm.read(4))[0]  # uint32
            update_counter = struct.unpack('I', shm.read(4))[0]  # uint32

            # Read position and orientation
            latitude = struct.unpack('d', shm.read(8))[0]  # double
            longitude = struct.unpack('d', shm.read(8))[0]
            altitude = struct.unpack('d', shm.read(8))[0]
            height = struct.unpack('d', shm.read(8))[0]
            pitch = struct.unpack('d', shm.read(8))[0]
            bank = struct.unpack('d', shm.read(8))[0]
            true_heading = struct.unpack('d', shm.read(8))[0]
            magnetic_heading = struct.unpack('d', shm.read(8))[0]

            # Read speeds
            indicated_airspeed = struct.unpack('d', shm.read(8))[0]
            ground_speed = struct.unpack('d', shm.read(8))[0]
            vertical_speed = struct.unpack('d', shm.read(8))[0]

            if data_valid:
                # Convert radians to degrees
                lat_deg = math.degrees(latitude)
                lon_deg = math.degrees(longitude)
                if lon_deg > 180:
                    lon_deg -= 360
                heading_deg = math.degrees(magnetic_heading) % 360

                # Convert m/s to knots
                ias_kts = indicated_airspeed * 1.94384

                # Display
                print(f"Pos: {lat_deg:.4f}, {lon_deg:.4f} | "
                      f"Alt: {altitude:.0f} ft | "
                      f"HDG: {heading_deg:.0f}° | "
                      f"IAS: {ias_kts:.0f} kts | "
                      f"Update: #{update_counter}")

            time.sleep(0.1)  # 10 Hz is enough for display

    except KeyboardInterrupt:
        print("\nClosing...")
    finally:
        shm.close()

if __name__ == "__main__":
    main()
```

### C/C++ Example (Windows)

```cpp
#include <windows.h>
#include <stdio.h>
#include <cmath>

// Structure matching the DLL data
#pragma pack(push, 1)
struct AeroflyReaderData {
    uint64_t timestamp_us;
    uint32_t data_valid;
    uint32_t update_counter;
    double latitude;
    double longitude;
    double altitude;
    double height;
    double pitch;
    double bank;
    double true_heading;
    double magnetic_heading;
    double indicated_airspeed;
    double ground_speed;
    double vertical_speed;
    // ... more fields as needed
};
#pragma pack(pop)

int main() {
    // Open shared memory
    HANDLE hMapFile = OpenFileMappingA(
        FILE_MAP_READ,
        FALSE,
        "AeroflyReaderData"
    );

    if (hMapFile == NULL) {
        printf("Error: Could not open shared memory.\n");
        printf("Make sure Aerofly is running.\n");
        return 1;
    }

    // Map to our address space
    AeroflyReaderData* data = (AeroflyReaderData*)MapViewOfFile(
        hMapFile,
        FILE_MAP_READ,
        0, 0,
        sizeof(AeroflyReaderData)
    );

    if (data == NULL) {
        printf("Error: Could not map memory.\n");
        CloseHandle(hMapFile);
        return 1;
    }

    printf("Connected! Press Ctrl+C to exit.\n\n");

    while (true) {
        if (data->data_valid) {
            // Convert units
            double lat_deg = data->latitude * 180.0 / 3.14159265359;
            double lon_deg = data->longitude * 180.0 / 3.14159265359;
            if (lon_deg > 180) lon_deg -= 360;

            double heading = fmod(data->magnetic_heading * 180.0 / 3.14159265359, 360.0);
            double ias_kts = data->indicated_airspeed * 1.94384;

            printf("Lat: %.4f | Lon: %.4f | Alt: %.0f ft | HDG: %.0f | IAS: %.0f kts\r",
                   lat_deg, lon_deg, data->altitude, heading, ias_kts);
        }

        Sleep(100);  // 10 Hz
    }

    UnmapViewOfFile(data);
    CloseHandle(hMapFile);
    return 0;
}
```

### C# Example (Windows)

```csharp
using System;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using System.Threading;

class Program
{
    // Structure matching the DLL data
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    struct AeroflyReaderData
    {
        public ulong timestamp_us;
        public uint data_valid;
        public uint update_counter;
        public double latitude;
        public double longitude;
        public double altitude;
        public double height;
        public double pitch;
        public double bank;
        public double true_heading;
        public double magnetic_heading;
        public double indicated_airspeed;
        public double ground_speed;
        public double vertical_speed;
        // ... add more fields as needed
    }

    static void Main()
    {
        try
        {
            // Open shared memory
            var mmf = MemoryMappedFile.OpenExisting("AeroflyReaderData");
            var accessor = mmf.CreateViewAccessor(0, Marshal.SizeOf<AeroflyReaderData>());

            Console.WriteLine("Connected! Press Ctrl+C to exit.\n");

            while (true)
            {
                // Read complete structure
                accessor.Read(0, out AeroflyReaderData data);

                if (data.data_valid == 1)
                {
                    // Convert units
                    double latDeg = data.latitude * 180.0 / Math.PI;
                    double lonDeg = data.longitude * 180.0 / Math.PI;
                    if (lonDeg > 180) lonDeg -= 360;

                    double heading = (data.magnetic_heading * 180.0 / Math.PI) % 360;
                    double iasKts = data.indicated_airspeed * 1.94384;

                    Console.Write($"\rLat: {latDeg:F4} | Lon: {lonDeg:F4} | " +
                                  $"Alt: {data.altitude:F0} ft | HDG: {heading:F0}° | " +
                                  $"IAS: {iasKts:F0} kts     ");
                }

                Thread.Sleep(100);
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error: {ex.Message}");
            Console.WriteLine("Make sure Aerofly is running with the DLL.");
        }
    }
}
```

---

## 5. Unit Conversion

Data comes from the simulator in specific units. Here's how to convert them:

### Position

| Data | Comes in | To get | Formula |
|------|----------|--------|---------|
| Latitude | Radians | Degrees | `lat_deg = lat_rad × 180 ÷ π` |
| Longitude | Radians | Degrees | `lon_deg = lon_rad × 180 ÷ π` |
| Longitude | 0-360 | -180 to +180 | If `lon > 180`: `lon = lon - 360` |

### Speeds

| Data | Comes in | To get | Formula |
|------|----------|--------|---------|
| Speeds | m/s | Knots (kts) | `kts = m_s × 1.94384` |
| Speeds | m/s | km/h | `kmh = m_s × 3.6` |
| Speeds | m/s | mph | `mph = m_s × 2.23694` |
| Vertical speed | m/s | ft/min | `fpm = m_s × 196.85` |

### Angles

| Data | Comes in | To get | Formula |
|------|----------|--------|---------|
| Pitch, Bank, Heading | Radians | Degrees | `deg = rad × 180 ÷ π` |
| Heading | Degrees | 0-360 | `hdg = deg % 360` (modulo) |

### Altitude

| Data | Comes in | Notes |
|------|----------|-------|
| Altitude (MSL) | Feet | Already in feet, no conversion needed |
| Height (AGL) | Feet | Already in feet, no conversion needed |

### Quick Conversion Functions (Python)

```python
import math

def rad_to_deg(radians):
    """Radians to degrees"""
    return radians * 180 / math.pi

def ms_to_kts(ms):
    """Meters per second to knots"""
    return ms * 1.94384

def ms_to_fpm(ms):
    """Meters per second to feet per minute"""
    return ms * 196.85

def normalize_longitude(lon_deg):
    """Normalize longitude from 0-360 to -180/+180"""
    if lon_deg > 180:
        return lon_deg - 360
    return lon_deg

def format_heading(radians):
    """Radians to heading 000-360"""
    degrees = rad_to_deg(radians) % 360
    return f"{degrees:03.0f}"
```

---

## 6. Complete Practical Examples

### Example 1: Display altitude on 7-segment display (Arduino)

```cpp
#include <WiFi.h>
#include <ArduinoJson.h>
#include <TM1637Display.h>

// Display pins
#define CLK 22
#define DIO 23

TM1637Display display(CLK, DIO);
WiFiClient client;

void setup() {
    Serial.begin(115200);
    display.setBrightness(7);

    WiFi.begin("YOUR_SSID", "YOUR_PASSWORD");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    client.connect("192.168.1.100", 12345);
}

void loop() {
    static String buffer = "";

    while (client.available()) {
        char c = client.read();
        buffer += c;

        if (c == '\n') {
            StaticJsonDocument<1024> doc;
            if (deserializeJson(doc, buffer) == DeserializationOk) {
                int altitude = doc["altitude"];

                // Show on display (divide by 100 if too large)
                if (altitude > 9999) {
                    display.showNumberDec(altitude / 100);
                } else {
                    display.showNumberDec(altitude);
                }
            }
            buffer = "";
        }
    }
    delay(50);
}
```

### Example 2: Record flight to CSV

```python
import socket
import json
import csv
import math
from datetime import datetime

def main():
    # Create CSV file
    filename = f"flight_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"

    with open(filename, 'w', newline='') as csvfile:
        writer = csv.writer(csvfile)

        # Headers
        writer.writerow([
            'Timestamp', 'Latitude', 'Longitude', 'Altitude_ft',
            'Heading', 'IAS_kts', 'GS_kts', 'VS_fpm',
            'Pitch', 'Bank', 'Throttle', 'Gear', 'Flaps'
        ])

        # Connect
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect(('localhost', 12345))

        buffer = ""
        last_write = 0

        print(f"Recording flight to {filename}...")
        print("Press Ctrl+C to stop.")

        try:
            while True:
                buffer += sock.recv(4096).decode('utf-8')

                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    if not line.strip():
                        continue

                    data = json.loads(line)

                    # Record every second
                    now = data.get('timestamp', 0)
                    if now - last_write >= 1000:  # 1000 ms
                        last_write = now

                        # Convert values
                        lat = math.degrees(data['latitude'])
                        lon = math.degrees(data['longitude'])
                        if lon > 180:
                            lon -= 360

                        writer.writerow([
                            datetime.now().isoformat(),
                            f"{lat:.6f}",
                            f"{lon:.6f}",
                            f"{data['altitude']:.0f}",
                            f"{math.degrees(data['magnetic_heading']) % 360:.0f}",
                            f"{data['indicated_airspeed'] * 1.94384:.0f}",
                            f"{data['ground_speed'] * 1.94384:.0f}",
                            f"{data['vertical_speed'] * 196.85:.0f}",
                            f"{math.degrees(data['pitch']):.1f}",
                            f"{math.degrees(data['bank']):.1f}",
                            f"{data['throttle'] * 100:.0f}",
                            f"{data['gear']:.2f}",
                            f"{data['flaps']:.2f}"
                        ])
                        csvfile.flush()  # Save immediately

        except KeyboardInterrupt:
            print(f"\nFlight saved to {filename}")
        finally:
            sock.close()

if __name__ == "__main__":
    main()
```

### Example 3: Simple web dashboard (HTML + JavaScript)

Create a file `dashboard.html`:

```html
<!DOCTYPE html>
<html>
<head>
    <title>Aerofly Dashboard</title>
    <style>
        body {
            font-family: 'Segoe UI', Arial, sans-serif;
            background: #1a1a2e;
            color: #eee;
            padding: 20px;
        }
        .container {
            max-width: 800px;
            margin: 0 auto;
        }
        h1 {
            color: #00d4ff;
            text-align: center;
        }
        .grid {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 15px;
        }
        .card {
            background: #16213e;
            border-radius: 10px;
            padding: 20px;
            text-align: center;
        }
        .card .label {
            color: #888;
            font-size: 12px;
            text-transform: uppercase;
        }
        .card .value {
            font-size: 32px;
            font-weight: bold;
            color: #00d4ff;
            margin: 10px 0;
        }
        .card .unit {
            color: #666;
            font-size: 14px;
        }
        #status {
            text-align: center;
            padding: 10px;
            margin-bottom: 20px;
        }
        .connected { color: #4caf50; }
        .disconnected { color: #f44336; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Aerofly FS4 Dashboard</h1>
        <div id="status" class="disconnected">Disconnected</div>

        <div class="grid">
            <div class="card">
                <div class="label">Altitude</div>
                <div class="value" id="altitude">---</div>
                <div class="unit">ft</div>
            </div>
            <div class="card">
                <div class="label">Speed</div>
                <div class="value" id="speed">---</div>
                <div class="unit">kts</div>
            </div>
            <div class="card">
                <div class="label">Heading</div>
                <div class="value" id="heading">---</div>
                <div class="unit">°</div>
            </div>
            <div class="card">
                <div class="label">Vertical Speed</div>
                <div class="value" id="vs">---</div>
                <div class="unit">fpm</div>
            </div>
            <div class="card">
                <div class="label">Pitch</div>
                <div class="value" id="pitch">---</div>
                <div class="unit">°</div>
            </div>
            <div class="card">
                <div class="label">Bank</div>
                <div class="value" id="bank">---</div>
                <div class="unit">°</div>
            </div>
        </div>

        <p style="text-align: center; margin-top: 30px; color: #666;">
            Aircraft: <span id="aircraft">---</span>
        </p>
    </div>

    <script>
        // To use this dashboard, you need a WebSocket server
        // that bridges between TCP and WebSocket.
        // You can use the included "websocket_bridge.py" example.

        function connect() {
            const ws = new WebSocket('ws://localhost:8765');

            ws.onopen = () => {
                document.getElementById('status').textContent = 'Connected';
                document.getElementById('status').className = 'connected';
            };

            ws.onclose = () => {
                document.getElementById('status').textContent = 'Disconnected - Reconnecting...';
                document.getElementById('status').className = 'disconnected';
                setTimeout(connect, 2000);
            };

            ws.onmessage = (event) => {
                const data = JSON.parse(event.data);

                // Convert and display
                const altitude = data.altitude.toFixed(0);
                const speed = (data.indicated_airspeed * 1.94384).toFixed(0);
                const heading = ((data.magnetic_heading * 180 / Math.PI) % 360).toFixed(0);
                const vs = (data.vertical_speed * 196.85).toFixed(0);
                const pitch = (data.pitch * 180 / Math.PI).toFixed(1);
                const bank = (data.bank * 180 / Math.PI).toFixed(1);

                document.getElementById('altitude').textContent = altitude;
                document.getElementById('speed').textContent = speed;
                document.getElementById('heading').textContent = heading.padStart(3, '0');
                document.getElementById('vs').textContent = (vs > 0 ? '+' : '') + vs;
                document.getElementById('pitch').textContent = pitch;
                document.getElementById('bank').textContent = bank;
                document.getElementById('aircraft').textContent = data.aircraft_name;
            };
        }

        connect();
    </script>
</body>
</html>
```

And the WebSocket bridge server (`websocket_bridge.py`):

```python
import asyncio
import websockets
import socket
import json

# Connected WebSocket clients
clients = set()

async def websocket_handler(websocket, path):
    """Handles WebSocket connections"""
    clients.add(websocket)
    print(f"Client connected. Total: {len(clients)}")
    try:
        await websocket.wait_closed()
    finally:
        clients.remove(websocket)
        print(f"Client disconnected. Total: {len(clients)}")

async def tcp_reader():
    """Reads from DLL via TCP and sends to all WebSocket clients"""
    while True:
        try:
            reader, writer = await asyncio.open_connection('localhost', 12345)
            print("Connected to DLL via TCP")

            buffer = ""
            while True:
                data = await reader.read(4096)
                if not data:
                    break

                buffer += data.decode('utf-8')

                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    if line.strip() and clients:
                        # Send to all WebSocket clients
                        websockets.broadcast(clients, line)

        except Exception as e:
            print(f"TCP Error: {e}. Reconnecting in 2s...")
            await asyncio.sleep(2)

async def main():
    # Start WebSocket server
    ws_server = await websockets.serve(websocket_handler, "localhost", 8765)
    print("WebSocket server at ws://localhost:8765")

    # Start TCP reader
    tcp_task = asyncio.create_task(tcp_reader())

    await asyncio.gather(ws_server.wait_closed(), tcp_task)

if __name__ == "__main__":
    asyncio.run(main())
```

---

## 7. Troubleshooting

### Can't connect (TCP)

**Error:** `Connection refused`

**Solution:**
1. Verify that Aerofly FS4 is running
2. Verify that the DLL is in the correct folder:
   `Documents\Aerofly FS 4\external_dll\AeroflyReader.dll`
3. Restart Aerofly after copying the DLL

### Can't connect (Shared Memory)

**Error:** `FileNotFoundError` or `Could not open shared memory`

**Solution:**
1. Verify that Aerofly is running with an active flight
2. The DLL creates shared memory when it receives the first data

### Data is not updating

**Cause:** You're probably in Aerofly's main menu.

**Solution:** Start a flight. Data is only sent during an active flight.

### Latitude/Longitude values look strange

**Cause:** They're in radians, not degrees.

**Solution:** Convert to degrees:
```python
lat_deg = lat_rad * 180 / 3.14159265359
lon_deg = lon_rad * 180 / 3.14159265359
if lon_deg > 180:
    lon_deg -= 360
```

### Speed seems too low

**Cause:** It's in meters per second, not knots.

**Solution:** Multiply by 1.94384 to get knots.

### I want to use a different TCP port

**Solution:** Set the environment variable before starting Aerofly:

```powershell
$env:AEROFLY_READER_TCP_PORT = "8888"
# Then start Aerofly
```

### I want fewer updates (save CPU)

**Solution:** Increase the broadcast interval:

```powershell
# 100 ms = 10 updates per second
$env:AEROFLY_READER_BROADCAST_MS = "100"
```

---

## Additional Resources

- **Included examples:** Check the `simplified/examples/` folder for more examples
- **Technical README:** `simplified/README.md` has all the technical details
- **Source code:** `simplified/aerofly_reader_dll.cpp` if you want to see how it works internally

---

**Version:** 1.1.0
**Platform:** Windows x64
**Compatibility:** Aerofly FS 4
