# Aerofly FS4 Bridge DLL

**Multi-Interface Bridge for Aerofly FS 4 Flight Simulator**

Copyright (c) 2025 Juan Luis Gabriel

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)]()
[![Aerofly FS 4](https://img.shields.io/badge/Aerofly%20FS-4-orange.svg)]()

A comprehensive, open-source bridge that enables real-time data exchange between Aerofly FS 4 and external applications through **three simultaneous interfaces**: Shared Memory, TCP, and WebSocket.

## 🚀 Quick Start

### For Users (Ready to Use)

1. **Download** the compiled DLL from [GitHub Releases](https://github.com/jlgabriel/Aerofly-FS4-Bridge/releases)
2. **Copy** `AeroflyBridge.dll` to:
   ```
   %USERPROFILE%\Documents\Aerofly FS 4\external_dll\AeroflyBridge.dll
   ```
3. **Launch** Aerofly FS 4 - the bridge activates automatically
4. **Connect** your applications:
   - **Shared Memory**: `"AeroflyBridgeData"` (fastest)
   - **TCP**: Port 12345 (JSON data), Port 12346 (commands)
   - **WebSocket**: Port 8765 (web/mobile apps)

### For Developers (Build from Source)

```bash
# Clone or download the repository
git clone https://github.com/jlgabriel/Aerofly-FS4-Bridge.git

# Compile using the provided script (recommended)
cd Aerofly-FS4-Bridge
scripts\compile.bat

# Or manual compilation (x64 Native Tools Command Prompt for VS 2022)
cl.exe /LD /EHsc /O2 /DNDEBUG /std:c++17 /DWIN32 /D_WINDOWS /D_USRDLL aerofly_bridge_dll.cpp /Fe:AeroflyBridge.dll /link ws2_32.lib kernel32.lib user32.lib
```

For detailed setup, see the Installation Guide in this docs folder: `installation_guide.md`.

### 📋 **Prerequisites for Building**
⚠️ **Important**: To build from source, you must download the Aerofly SDK header:

1. **Download** `tm_external_message.h` from [IPACS Developer Site](https://www.aerofly.com/developers/) (section "External DLL")
2. **Place** the file in the project root directory
3. **Note**: This file is not included in the repository due to IPACS licensing restrictions

## ✨ Features

### 🔗 **Triple Interface Architecture**
- **Shared Memory**: Ultra-fast local access for performance-critical applications
- **TCP Server**: Network-accessible JSON API for remote monitoring and control
- **WebSocket Server**: Native browser and mobile app connectivity

### 📊 **Comprehensive Data Access**
- **361 variables** exposed with canonical Aerofly SDK names (e.g., `"Aircraft.Altitude"`)
- **Real-time updates**: 50Hz telemetry streaming (configurable)
- **Bidirectional control**: Read telemetry + send commands
- **JSON protocol**: Consistent format across TCP and WebSocket

### 🛠 **Developer-Friendly**
- **Thread-safe**: Production-ready concurrent access
- **No Dependencies**: WebSocket implementation built-in, no external libraries
- **Graceful Fallbacks**: Interfaces fail independently
- **Environment Config**: Ports and settings via environment variables

## 📖 Documentation

### Quick Reference
- **Installation Guide** (`docs/installation_guide.md`): Step-by-step setup and build instructions.
- **API Reference** (`docs/api_reference.md`): Complete interface documentation for Shared Memory, TCP, and WebSocket.
- **Variables Reference** (`docs/variables_reference.md`): All 361 available variables with canonical names.
- **JSON Schema** (`reference/json_schema.json`): Telemetry payload structure.
- **Release Guide** (`docs/release_guide.md`): How to build the DLL and publish a GitHub Release.

### Learning Resources
- **DLL Development Basics** (`docs/dll_basics_tutorial.md`): Create Aerofly DLLs from scratch.
- **Architecture Deep Dive** (`docs/architecture_deep_dive.md`): Internals and design decisions.
- **Thread-Safe Programming** (`docs/thread_safety_tutorial.md`): Synchronization patterns for real-time systems.
- **Network Programming Patterns** (`docs/network_programming_tutorial.md`): TCP/WebSocket servers from scratch.
- **Performance Optimization** (`docs/performance_optimization_tutorial.md`): Measuring and tuning for 50+ Hz.
- **Web App Tutorial** (`docs/web_app_tutorial.md`): Build a browser dashboard using WebSocket.
- **Python App Tutorial** (`docs/python_app_tutorial.md`): Read telemetry via Shared Memory and TCP.

### Examples

This repository includes two convenience areas for learning-by-doing:

- `tutorials/`: Tutorial guides (Markdown) that cover Shared Memory, TCP, and WebSocket usage.
- `web/`: Standalone HTML demos that you can open directly in a browser when the WebSocket interface is enabled.

Highlights in `tutorials/`:
- `tutorial_websocket_altitude_monitor.md` — Real-time altitude monitor in the browser using canonical variable `Aircraft.Altitude` with simple low-altitude warnings.
- `tutorial_websocket_copilot_checklist.md` — Co‑Pilot checklist dashboard that evaluates takeoff/landing rules from canonical variables (parking brake, flaps, throttle, IAS, vertical speed, headings).
- `tutorial_websocket_airspeed_envelope.md` — Airspeed tape that visualizes performance envelopes from `Performance.Speed.*` (VS0, VFE, VNO, VNE, VAPP).
- `tutorial_python_flight_logger.md` — Logging complete JSON frames to CSV with basic flight statistics.
- `tutorial_python_maps_bridge.md` — Maps integration and position tracking.

#### Tutorials index (one‑liners)
- `tutorial_websocket_altitude_monitor.md`: Build a minimal web altitude monitor that warns below 1000/500 ft.
- `tutorial_websocket_copilot_checklist.md`: Implement a browser checklist that turns items green/yellow/red from canonical variables.
- `tutorial_websocket_airspeed_envelope.md`: Draw an airspeed tape with VS0/VFE/VNO/VNE arcs and VAPP marker in `<canvas>`.
- `tutorial_python_flight_logger.md`: Consume TCP JSON frames, log to CSV, and compute flight stats (time, distance, maxima).
- `tutorial_python_maps_bridge.md`: Maps integration and position tracking.

Standalone browser demos in `web/` (no build tools):
- `altitude_monitor.html` — Drop‑in altitude display with color‑coded cautions.
- `copilot_checklist.html` — Rule‑based takeoff/landing indicators.
- `airspeed_envelope.html` — Horizontal airspeed tape with white/green/yellow/red arcs and a VAPP marker.

Notes for running the examples:
- Web demos require the WebSocket interface to be active. Ensure `AEROFLY_BRIDGE_WS_ENABLE=1`. Default port is `8765` and can be changed with `AEROFLY_BRIDGE_WS_PORT`.
- Python TCP samples expect the data stream on port `12345` and the command channel on port `12346`.
- Shared memory readers expect a mapping named `AeroflyBridgeData` and may use the offsets metadata file `AeroflyBridge_offsets.json` for names/indices.

#### Python folder quick runs (`python/`)
- Realtime monitor (Shared Memory UI):
  ```bash
  python python/aerofly_realtime_monitor.py AeroflyBridge_offsets.json
  ```
- Master control panel (TCP commands on 12346):
  ```bash
  python python/master_control_panel.py
  ```
- Maps/position tracker (TCP data on 12345):
  ```bash
  python python/aerofly_fs4_maps_bridge.py
  ```
- Flight logger (TCP data to CSV):
  ```bash
  python python/flight_logger.py
  ```

Run examples (Python):
- Realtime monitor (needs offsets JSON):
  ```bash
  python python/aerofly_realtime_monitor.py AeroflyBridge_offsets.json
  ```
- Master control panel (connects to command port 12346):
  ```bash
  python python/master_control_panel.py
  ```

#### Getting the offsets JSON

- The file `AeroflyBridge_offsets.json` is created next to `AeroflyBridge.dll` and describes the shared-memory layout (base, stride, per-variable offsets).
- Copy it to your working directory or pass an absolute path to the realtime monitor:
  ```bash
  python examples/aerofly_realtime_monitor.py C:\path\to\AeroflyBridge_offsets.json
  ```
  If you don't have it, check the DLL output folder or your release assets.

## 🌟 Use Cases

### 🎮 **Real-time Dashboards**
Create web or mobile dashboards displaying live flight data:
```javascript
// WebSocket example (canonical variables)
const ws = new WebSocket('ws://localhost:8765');
ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  const altitude_m = data.variables["Aircraft.Altitude"]; // meters
  const ias_mps = data.variables["Aircraft.IndicatedAirspeed"]; // m/s
  updateAltitude(altitude_m);
  updateSpeed(ias_mps);
};
```

### 🔧 **Hardware Integration**
Connect physical panels and controls:
```cpp
// Shared memory example (C++)
HANDLE hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, "AeroflyBridgeData");
AeroflyBridgeData* pData = (AeroflyBridgeData*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);
double altitude_m = pData->aircraft_altitude;
```

### 📱 **Mobile Applications**
Build companion apps for tablets and phones:
```python
# Python TCP example (canonical variables)
import socket, json
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(("localhost", 12345))
data = json.loads(sock.recv(16384).decode())
print(f"Altitude: {data['variables']['Aircraft.Altitude']} m")
```

### 📈 **Data Logging & Analytics**
Record flights for analysis and training:
```json
{
  "schema": "aerofly-bridge-telemetry",
  "schema_version": 1,
  "timestamp": 1640995200000000,
  "timestamp_unit": "microseconds",
  "data_valid": 1,
  "broadcast_rate_hz": 50.0,
  "variables": {
    "Aircraft.UniversalTime": 123.456,
    "Aircraft.Altitude": 3500.0,
    "Aircraft.IndicatedAirspeed": 120.5,
    "Aircraft.MagneticHeading": 1.5708,
    "Controls.Throttle": 0.8,
    "Navigation.NAV1Frequency": 108500000.0
    // ... all 361 variables with descriptive names
  }
}
```

## 🔧 Configuration

The bridge supports configuration via environment variables:

```bash
# WebSocket settings
set AEROFLY_BRIDGE_WS_ENABLE=1        # Enable WebSocket (default: 1)
set AEROFLY_BRIDGE_WS_PORT=8765       # WebSocket port (default: 8765)

# Performance tuning
set AEROFLY_BRIDGE_BROADCAST_MS=20    # Broadcast interval (default: 20ms)
```

## 🎯 Project Goals

### **Production Use**
This bridge is designed for real-world applications:
- Flight training tools
- Hardware integration
- Mobile companion apps
- Data logging systems
- Stream overlays

### **Educational Purpose**
Learn professional DLL development for flight simulators:
- Modern C++ practices
- Multi-threading patterns
- Network programming
- Memory management
- Performance optimization

## 🛡️ Requirements

- **Aerofly FS 4** (any version with external DLL support)
- **Windows** (Windows 10/11 recommended)
- **Visual Studio 2022** (Community, Professional, or Enterprise - for building from source)
- **Windows SDK** (latest version)

**Note**: The compiled DLL includes `/DNDEBUG` flag to prevent assert() breakpoint exceptions on Windows 10.

## 📄 License

This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details.

**Key permissions:**
- ✅ Educational use
- ✅ Private use
- ✅ Commercial use
- ✅ Modification and distribution


**Legal note:** Despite AI authorship, this project remains licensed under MIT with no additional restrictions; standard copyright and licensing notices apply as stated in the LICENSE file.

## 🤖 AI Authorship

All source code and all documentation in this repository were generated by artificial intelligence (Claude Sonnet 4 and ChatGPT 5), using web tooling and the Cursor IDE.

## 🙏 Acknowledgments

- **Juan Luis Gabriel** - Project creator and lead developer
- **Aerofly FS Team** for the excellent flight simulator and SDK
- **Flight simulation community** for inspiration and feedback
- **Open source contributors** who make projects like this possible

## 📞 Support

- Documentation: See the files in `docs/` and examples in `examples/` within this repository.
- Issues: Report bugs or request features via GitHub Issues.
- Community support: Provided by the Aerofly FS 4 user community on the official IPACS forum thread — [Aerofly FS4 Bridge DLL](https://www.aerofly.com/community/forum/index.php?thread/25954-aerofly-fs4-bridge-dll/).

See `CHANGELOG.md` for release notes and version history.

---

## 📚 Documentation Index

### Core Reference
- **`api_reference.md`** - Complete interface documentation (Shared Memory, TCP, WebSocket)
- **`variables_reference.md`** - All 361 available variables with canonical names
- **`installation_guide.md`** - Step-by-step setup and build instructions

### Architecture & Development
- **`architecture_deep_dive.md`** - Multi-interface architecture and data flow internals
- **`dll_basics_tutorial.md`** - Create Aerofly DLLs from scratch
- **`thread_safety_tutorial.md`** - Threading model and safe concurrent access patterns
- **`network_programming_tutorial.md`** - TCP/WebSocket implementations and design patterns
- **`performance_optimization_tutorial.md`** - Performance budgets, profiling, and optimization

### Application Development
- **`web_app_tutorial.md`** - Build a browser dashboard using WebSocket
- **`python_app_tutorial.md`** - Read telemetry via Shared Memory and TCP

### Project Management
- **`release_guide.md`** - How to build the DLL and publish a GitHub Release

All paths are relative to this repository. See `../reference/json_schema.json` for the JSON schema reference.

---

Copyright (c) 2025 Juan Luis Gabriel