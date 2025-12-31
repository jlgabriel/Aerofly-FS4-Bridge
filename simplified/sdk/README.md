# Aerofly Reader SDK

A Python SDK for easy integration with the AeroflyReader DLL, providing typed access to Aerofly FS4 flight telemetry data.

## Features

- **Typed Data Models** - Full autocompletion and type hints
- **Automatic Unit Conversions** - Radians to degrees, m/s to knots, etc.
- **Multiple Clients** - TCP (sync/async) and Shared Memory
- **Context Managers** - Clean resource management with `with` statements
- **Auto-reconnect** - Handles connection drops gracefully
- **Zero Dependencies** - Uses only Python standard library

## Installation

```bash
# From the SDK directory
pip install -e .

# Or copy the aerofly_reader folder to your project
```

## Quick Start

### Streaming Data (Recommended)

```python
from aerofly_reader import stream

for flight in stream():
    print(f"Aircraft: {flight.aircraft_name}")
    print(f"Position: {flight.position.latitude:.4f}°, {flight.position.longitude:.4f}°")
    print(f"Altitude: {flight.position.altitude_ft:.0f} ft")
    print(f"Speed: {flight.speeds.indicated_airspeed:.0f} kts")
```

### Using the Client

```python
from aerofly_reader import AeroflyClient

with AeroflyClient() as client:
    for flight in client.stream():
        if flight.on_ground:
            print("Aircraft is on the ground")
        else:
            print(f"Flying at {flight.position.altitude_ft:.0f} ft")
```

### One-Shot Read

```python
from aerofly_reader import read_once

data = read_once()
print(f"Current altitude: {data.position.altitude_ft:.0f} ft")
```

### Async Usage

```python
import asyncio
from aerofly_reader import AsyncAeroflyClient

async def main():
    async with AsyncAeroflyClient() as client:
        async for flight in client.stream():
            print(f"Alt: {flight.position.altitude_ft:.0f} ft")

asyncio.run(main())
```

### Shared Memory (Windows, Lowest Latency)

```python
from aerofly_reader import SharedMemoryClient

with SharedMemoryClient() as client:
    data = client.read()
    print(f"Latency: ~0.01ms")
```

## Data Models

The SDK provides typed dataclasses with automatic unit conversions:

### FlightData

Main container for all flight data:

```python
flight.timestamp          # Microsecond timestamp
flight.update_counter     # Frame counter
flight.data_valid         # True if data is valid
flight.update_hz          # Update frequency

flight.position           # Position object
flight.speeds             # Speeds object
flight.orientation        # Orientation object
flight.engines            # Engines object
flight.autopilot          # Autopilot object
flight.navigation         # Radio frequencies
flight.vspeeds            # V-speeds

flight.on_ground          # True if on ground
flight.gear               # 0.0 (up) to 1.0 (down)
flight.flaps              # 0.0 to 1.0
flight.throttle           # 0.0 to 1.0
flight.parking_brake      # True if set

flight.aircraft_name      # Aircraft name
```

### Position

```python
pos = flight.position

# Raw values (as received from DLL)
pos.latitude_rad     # Radians
pos.longitude_rad    # Radians
pos.altitude_ft      # Feet MSL
pos.height_agl_ft    # Feet AGL

# Auto-converted properties
pos.latitude         # Degrees (-90 to +90)
pos.longitude        # Degrees (-180 to +180)
pos.altitude         # Alias for altitude_ft
pos.height           # Alias for height_agl_ft
```

### Speeds

```python
spd = flight.speeds

# Raw values (m/s)
spd.indicated_airspeed_ms
spd.ground_speed_ms
spd.vertical_speed_ms

# Auto-converted properties
spd.indicated_airspeed  # Knots
spd.ias                 # Alias for indicated_airspeed
spd.ground_speed        # Knots
spd.gs                  # Alias for ground_speed
spd.vertical_speed      # Feet per minute
spd.vs                  # Alias for vertical_speed
spd.mach                # Mach number
spd.angle_of_attack     # Degrees
```

### Orientation

```python
ori = flight.orientation

# Raw values (radians)
ori.pitch_rad
ori.bank_rad
ori.true_heading_rad
ori.magnetic_heading_rad

# Auto-converted properties
ori.pitch              # Degrees
ori.bank               # Degrees
ori.roll               # Alias for bank
ori.heading            # Magnetic heading (0-360)
ori.true_heading       # True heading (0-360)
ori.magnetic_heading   # Same as heading
```

### Engines

```python
eng = flight.engines

eng.engine1.running      # bool
eng.engine1.throttle     # 0.0 to 1.0
eng.engine2.running
eng.engine2.throttle

eng.any_running          # True if any engine running
eng.all_running          # True if all engines running
```

### Autopilot

```python
ap = flight.autopilot

ap.master          # True if AP enabled
ap.heading         # Selected heading (degrees)
ap.altitude        # Selected altitude (feet)
ap.vertical_speed  # Selected VS (fpm)
```

### Vector3D (Physics Vectors)

The SDK includes 4 physics vectors as `Vector3D` objects, each with `x`, `y`, `z` components:

```python
# Available vectors in FlightData
flight.world_position   # Aircraft position in world coordinates
flight.velocity         # Velocity vector (m/s)
flight.acceleration     # Acceleration vector (m/s²)
flight.wind             # Wind vector (m/s)
```

**Vector3D Properties:**

```python
vec = flight.velocity

# Individual components
vec.x                # X component
vec.y                # Y component
vec.z                # Z component

# Calculated property
vec.magnitude        # Vector magnitude (√(x² + y² + z²))
```

**Example Usage:**

```python
from aerofly_reader import stream

for flight in stream():
    # Get velocity components
    print(f"Velocity X: {flight.velocity.x:.2f} m/s")
    print(f"Velocity Y: {flight.velocity.y:.2f} m/s")
    print(f"Velocity Z: {flight.velocity.z:.2f} m/s")

    # Calculate total speed from velocity vector
    total_speed = flight.velocity.magnitude
    print(f"Total Speed: {total_speed:.2f} m/s")

    # Wind information
    wind_speed = flight.wind.magnitude
    print(f"Wind Speed: {wind_speed:.2f} m/s")

    # Acceleration (useful for G-force calculations)
    g_force = flight.acceleration.magnitude / 9.81
    print(f"G-Force: {g_force:.2f} G")
```

**Vector3D is immutable** (`frozen=True`) for thread safety and data integrity.

## Unit Conversion Utilities

```python
from aerofly_reader import (
    ms_to_knots,
    ms_to_fpm,
    radians_to_degrees,
    normalize_heading,
    normalize_longitude,
    format_heading,
    format_altitude,
    format_speed_kts,
    format_vertical_speed,
)

# Convert values
speed_kts = ms_to_knots(50.0)      # 97.2 kts
vs_fpm = ms_to_fpm(5.0)            # 984.3 fpm
heading = normalize_heading(370)   # 10

# Format for display
format_heading(3.14159)            # "180°"
format_altitude(10500)             # "10,500 ft"
format_speed_kts(77.17)            # "150 kts"
format_vertical_speed(2.54)        # "+500 fpm"
```

## Client Options

```python
from aerofly_reader import AeroflyClient

client = AeroflyClient(
    host='localhost',           # Server hostname
    port=12345,                 # Server port
    timeout=5.0,                # Socket timeout (seconds)
    auto_reconnect=True,        # Auto-reconnect on disconnect
    reconnect_delay=2.0,        # Delay between reconnect attempts
    max_reconnect_attempts=5,   # Max attempts (0 = infinite)
)
```

## Error Handling

```python
from aerofly_reader import (
    AeroflyClient,
    ConnectionError,
    DisconnectedError,
    TimeoutError,
    DataError,
)

try:
    with AeroflyClient() as client:
        for flight in client.stream():
            process(flight)

except ConnectionError as e:
    print(f"Cannot connect: {e}")
except DisconnectedError as e:
    print(f"Connection lost: {e}")
except TimeoutError as e:
    print(f"Timeout: {e}")
```

## Examples

See the `examples/` folder:

| Example | Description |
|---------|-------------|
| `basic_usage.py` | Simple streaming display |
| `flight_logger_sdk.py` | CSV flight recorder |
| `async_monitor.py` | Async/await example |
| `one_shot_read.py` | Single frame read |

## Comparison: With vs Without SDK

**Without SDK:**
```python
import socket
import json
import math

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('localhost', 12345))
buffer = ""
data = sock.recv(4096).decode('utf-8')
buffer += data
line, buffer = buffer.split('\n', 1)
flight_data = json.loads(line)

lat = math.degrees(flight_data['latitude'])
lon = math.degrees(flight_data['longitude'])
if lon > 180:
    lon -= 360
speed_kts = flight_data['indicated_airspeed'] * 1.94384
vs_fpm = flight_data['vertical_speed'] * 196.85
heading = math.degrees(flight_data['magnetic_heading']) % 360
```

**With SDK:**
```python
from aerofly_reader import read_once

flight = read_once()
lat = flight.position.latitude
lon = flight.position.longitude
speed_kts = flight.speeds.indicated_airspeed
vs_fpm = flight.speeds.vertical_speed
heading = flight.orientation.heading
```

## Requirements

- Python 3.8+
- AeroflyReader.dll loaded in Aerofly FS4
- Windows (for Shared Memory), any platform (for TCP)

## License

MIT License - Same as main project.
