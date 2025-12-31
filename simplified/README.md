# Aerofly FS4 Reader DLL v1.1.0

A simplified and optimized version of Aerofly Bridge focused exclusively on **reading data** from the simulator. Ideal for integrations that only need to monitor flight status without controlling the aircraft.

## Key Features

- **Read-only** - No commands, no aircraft control
- **~900 lines of code** - vs 9,000+ in the complete bridge
- **High performance** - Optimized JSON builder with zero allocations
- **Two interfaces** - Shared Memory (local) + TCP Streaming (network)
- **Robust** - Automatic protection against NaN/Inf values
- **Pre-built binary** - Ready to use DLL in `bin/` folder
- **5 Python examples** - From simple readers to map trackers

## Quick Start (Pre-built DLL)

A pre-built DLL is available in the `bin/` folder for immediate use:

```powershell
# Install pre-built DLL directly
copy simplified\bin\AeroflyReader.dll "$env:USERPROFILE\Documents\Aerofly FS 4\external_dll\"
```

## Architecture

```
Aerofly FS4 (50-60 Hz)
        │
        │ tm_external_message
        ▼
┌─────────────────────────────────────────────────┐
│              AeroflyReader.dll                  │
│                                                 │
│  ┌─────────────────┐    ┌─────────────────┐    │
│  │ SharedMemory    │    │ TCPDataServer   │    │
│  │ "AeroflyReader  │    │ Port 12345      │    │
│  │  Data"          │    │ JSON @ 50 Hz    │    │
│  └────────┬────────┘    └────────┬────────┘    │
└───────────┼──────────────────────┼─────────────┘
            │                      │
            ▼                      ▼
     Local Apps              Network Clients
    (Python, C++)         (Python, Web, etc.)
```

## Data Delivery

### 1. Shared Memory (High Performance)

Direct memory access for local applications. Minimal latency (~0.01ms).

```python
import mmap
import struct

# Open shared memory
shm = mmap.mmap(-1, 1024, "AeroflyReaderData", access=mmap.ACCESS_READ)

while True:
    shm.seek(0)

    # Header (16 bytes)
    timestamp = struct.unpack('Q', shm.read(8))[0]
    data_valid = struct.unpack('I', shm.read(4))[0]
    update_counter = struct.unpack('I', shm.read(4))[0]

    # Position (doubles)
    lat = struct.unpack('d', shm.read(8))[0]
    lon = struct.unpack('d', shm.read(8))[0]
    alt = struct.unpack('d', shm.read(8))[0]

    if data_valid:
        print(f"Pos: {lat:.4f}, {lon:.4f} | Alt: {alt:.0f} ft")
```

### 2. TCP Streaming (Network)

JSON streaming for network clients. Configurable up to 50 Hz.

```python
import socket
import json

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('localhost', 12345))

buffer = ""
while True:
    buffer += sock.recv(4096).decode('utf-8')

    while '\n' in buffer:
        line, buffer = buffer.split('\n', 1)
        if line.strip():
            data = json.loads(line)
            print(f"Alt: {data['altitude']:.0f} ft @ {data['update_hz']:.1f} Hz")
```

## JSON Format

```json
{
    "schema": "aerofly-reader-telemetry",
    "version": "1.1.0",
    "update_hz": 49.8,
    "timestamp": 1234567890123,
    "data_valid": 1,
    "update_counter": 42,

    "latitude": 0.828321,
    "longitude": 4.147626,
    "altitude": 5280.0,
    "height": 1500.0,
    "pitch": 0.087266,
    "bank": -0.034907,
    "true_heading": 3.141593,
    "magnetic_heading": 3.089233,

    "indicated_airspeed": 77.17,
    "ground_speed": 82.31,
    "vertical_speed": 2.540,
    "mach_number": 0.2300,
    "angle_of_attack": 0.052360,

    "on_ground": 0,
    "gear": 0.00,
    "flaps": 0.00,
    "throttle": 0.65,
    "parking_brake": 0,

    "engine_running_1": 1,
    "engine_running_2": 1,
    "engine_throttle_1": 0.65,
    "engine_throttle_2": 0.65,

    "nav1_frequency": 112.500,
    "nav2_frequency": 108.000,
    "com1_frequency": 121.500,
    "com2_frequency": 118.000,

    "autopilot_master": 1,
    "autopilot_heading": 3.141593,
    "autopilot_altitude": 10000,

    "vs0": 23.15,
    "vs1": 28.29,
    "vfe": 43.72,
    "vno": 66.88,
    "vne": 82.31,

    "position": {"x": 123.45, "y": 567.89, "z": 910.11},
    "velocity": {"x": 10.50, "y": 0.00, "z": 2.50},

    "aircraft_name": "Cessna 172",
    "nearest_airport_id": "KSEA",
    "nearest_airport_name": "Seattle-Tacoma International"
}
```

> **Note on Latitude/Longitude**: Values are in **radians** as received from the simulator. To convert to degrees:
> - `lat_deg = lat_rad * (180 / pi)`
> - `lon_deg = lon_rad * (180 / pi)`
> - If `lon_deg > 180`: `lon_deg -= 360` (normalize to -180/+180)

### Special Fields

| Field | Description |
|-------|-------------|
| `version` | DLL version (e.g., "1.1.0") |
| `update_hz` | Calculated actual update frequency |
| `data_valid` | 1 = valid data, 0 = updating |

## Available Variables

### Position and Orientation
| Variable | Unit | Description |
|----------|------|-------------|
| `latitude` | radians | GPS latitude (convert to degrees, see note above) |
| `longitude` | radians | GPS longitude (convert to degrees and normalize) |
| `altitude` | feet | MSL altitude |
| `height` | feet | AGL height (above ground) |
| `pitch` | radians | Pitch |
| `bank` | radians | Bank/Roll |
| `true_heading` | radians | True heading |
| `magnetic_heading` | radians | Magnetic heading |

### Speeds
| Variable | Unit | Description |
|----------|------|-------------|
| `indicated_airspeed` | m/s | Indicated airspeed |
| `ground_speed` | m/s | Ground speed |
| `vertical_speed` | m/s | Vertical speed |
| `mach_number` | - | Mach number |
| `angle_of_attack` | radians | Angle of attack |

### Aircraft State
| Variable | Range | Description |
|----------|-------|-------------|
| `on_ground` | 0/1 | On ground |
| `gear` | 0-1 | Landing gear position |
| `flaps` | 0-1 | Flaps position |
| `throttle` | 0-1 | Overall power |
| `parking_brake` | 0/1 | Parking brake |

### Engines
| Variable | Range | Description |
|----------|-------|-------------|
| `engine_running_1` | 0/1 | Engine 1 running |
| `engine_running_2` | 0/1 | Engine 2 running |
| `engine_throttle_1` | 0-1 | Engine 1 throttle |
| `engine_throttle_2` | 0-1 | Engine 2 throttle |

### Navigation
| Variable | Unit | Description |
|----------|------|-------------|
| `nav1_frequency` | MHz | NAV1 frequency |
| `nav2_frequency` | MHz | NAV2 frequency |
| `com1_frequency` | MHz | COM1 frequency |
| `com2_frequency` | MHz | COM2 frequency |
| `selected_course_1` | radians | Selected course 1 |
| `selected_course_2` | radians | Selected course 2 |

### Autopilot (read-only)
| Variable | Unit | Description |
|----------|------|-------------|
| `autopilot_master` | 0/1 | AP active |
| `autopilot_heading` | radians | Selected heading |
| `autopilot_altitude` | feet | Selected altitude |

### V-Speeds
| Variable | Description |
|----------|-------------|
| `vs0` | Stall speed (flaps extended) |
| `vs1` | Stall speed (clean configuration) |
| `vfe` | Maximum flaps extended speed |
| `vno` | Maximum structural cruise speed |
| `vne` | Never exceed speed |

## Configuration

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `AEROFLY_READER_TCP_PORT` | 12345 | TCP port for streaming |
| `AEROFLY_READER_BROADCAST_MS` | 20 | Broadcast interval (ms) |

```powershell
# Example: change port and reduce frequency
$env:AEROFLY_READER_TCP_PORT = "8888"
$env:AEROFLY_READER_BROADCAST_MS = "100"  # 10 Hz
```

## Build from Source

### Requirements
- Windows 10/11
- Visual Studio 2022 (C++ desktop development)
- CMake 3.15+
- SDK Header: `tm_external_message.h`

### Steps

```powershell
# 1. Copy Aerofly SDK header to this directory
copy "path\to\tm_external_message.h" simplified\

# 2. Configure and build
cd simplified
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# 3. Install
copy build\Release\AeroflyReader.dll "$env:USERPROFILE\Documents\Aerofly FS 4\external_dll\"
```

## Performance Optimizations

### v1.1.0 Improvements

| Optimization | Benefit |
|--------------|---------|
| **JSON with snprintf** | 3-5x faster than ostringstream |
| **Static buffer** | Zero allocations per frame |
| **Handler macros** | 15% more compact code |
| **NaN/Inf protection** | Always valid JSON |

### Technical Features

- **Thread-local buffer**: Each thread has its own JSON buffer
- **Automatic validation**: NaN/Inf values converted to 0.0
- **Hash map O(1)**: Instant message handler lookup
- **Non-blocking sockets**: TCP doesn't block main thread

## Use Cases

| Application | Description |
|-------------|-------------|
| **Flight Logger** | Record flights to CSV/database |
| **Map Tracker** | Real-time position on maps |
| **Training Dashboard** | Metrics for training |
| **Stream Overlay** | OBS overlay with flight data |
| **Home Cockpit** | Feed physical instruments |

## Included Examples

```
simplified/
└── examples/
    ├── simple_reader.py          # Basic real-time console display
    ├── flight_logger.py          # CSV flight recording
    ├── aircraft_tracker.py       # Interactive map with aircraft position
    ├── advanced_flight_logger.py # CSV + statistics + takeoff/landing detection
    └── realtime_monitor.py       # Tkinter GUI with tree view of all variables
```

### Example Descriptions

| Example | Description | Requirements |
|---------|-------------|--------------|
| `simple_reader.py` | Console display of flight data | None (standard lib) |
| `flight_logger.py` | Records flight data to CSV file | None (standard lib) |
| `aircraft_tracker.py` | Shows aircraft on interactive map | `tkintermapview`, `pillow` |
| `advanced_flight_logger.py` | Flight logger with stats and event detection | None (standard lib) |
| `realtime_monitor.py` | Desktop GUI showing all variables in tree view | `tkinter` (included with Python) |

## Python SDK

For easier Python integration, a complete **Python SDK** is available in the `sdk/` folder. The SDK provides:

- **Typed Data Models** - Full autocompletion and type hints
- **Automatic Unit Conversions** - Radians to degrees, m/s to knots, etc.
- **Multiple Clients** - TCP (sync/async) and Shared Memory
- **Context Managers** - Clean resource management
- **Auto-reconnect** - Handles connection drops gracefully

### Quick Comparison

**Without SDK:**
```python
import socket, json, math

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('localhost', 12345))
buffer = ""
data = sock.recv(4096).decode('utf-8')
buffer += data
line, buffer = buffer.split('\n', 1)
flight_data = json.loads(line)

lat = math.degrees(flight_data['latitude'])
speed_kts = flight_data['indicated_airspeed'] * 1.94384
```

**With SDK:**
```python
from aerofly_reader import stream

for flight in stream():
    print(f"Lat: {flight.position.latitude}°")
    print(f"Speed: {flight.speeds.indicated_airspeed} kts")
```

### SDK Installation

```bash
cd simplified/sdk
pip install -e .
```

### SDK Structure

```
sdk/
├── aerofly_reader/        # Main package
│   ├── client.py          # TCP client (sync)
│   ├── async_client.py    # TCP client (async)
│   ├── shared_memory.py   # Shared memory client
│   ├── models.py          # FlightData, Position, Speeds, etc.
│   ├── units.py           # Unit conversions
│   └── exceptions.py      # Custom exceptions
├── examples/              # SDK usage examples
├── pyproject.toml         # Package configuration
└── README.md              # Full SDK documentation
```

See [`sdk/README.md`](sdk/README.md) for complete SDK documentation.

## Comparison with Complete Bridge

| Feature | Reader | Complete Bridge |
|---------|--------|-----------------|
| Lines of code | ~900 | ~9,000 |
| Variables | ~50 | 358 |
| Data reading | ✅ | ✅ |
| Aircraft control | ❌ | ✅ |
| Shared Memory | ✅ | ✅ |
| TCP Streaming | ✅ | ✅ |
| TCP Commands | ❌ | ✅ |
| WebSocket | ❌ | ✅ |
| C172 specific | ❌ | ✅ |
| Warnings | ❌ | ✅ |

## License

MIT License - Same as the main project.

---

**Version**: 1.1.0
**Compatibility**: Aerofly FS 4
**Platform**: Windows x64
