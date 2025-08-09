# Aerofly FS4 Bridge DLL

**Multi-Interface Bridge for Aerofly FS 4 Flight Simulator**

[![License](https://img.shields.io/badge/license-BSD--3--Clause-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)]()
[![Aerofly FS 4](https://img.shields.io/badge/Aerofly%20FS-4-orange.svg)]()

A comprehensive, open-source bridge that enables real-time data exchange between Aerofly FS 4 and external applications through **three simultaneous interfaces**: Shared Memory, TCP, and WebSocket.

## 🚀 Quick Start

### For Users (Ready to Use)

1. **Download** the compiled DLL from [Releases](releases/)
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
git clone https://github.com/yourusername/Aerofly-FS4-Bridge.git

# Compile using the provided script (recommended)
cd Aerofly-FS4-Bridge/src
compile.bat

# Or manual compilation (x64 Native Tools Command Prompt for VS 2022)
cl.exe /LD /EHsc /O2 /DNDEBUG /std:c++17 /DWIN32 /D_WINDOWS /D_USRDLL aerofly_bridge_dll_complete_estable.cpp /Fe:AeroflyBridge.dll /link ws2_32.lib kernel32.lib user32.lib
```

See [Build Instructions](docs/installation.md) for detailed setup.

## ✨ Features

### 🔗 **Triple Interface Architecture**
- **Shared Memory**: Ultra-fast local access for performance-critical applications
- **TCP Server**: Network-accessible JSON API for remote monitoring and control
- **WebSocket Server**: Native browser and mobile app connectivity

### 📊 **Comprehensive Data Access**
- **339+ Variables**: Complete aircraft state, navigation, autopilot, engine data
- **Real-time Updates**: 50Hz telemetry streaming (configurable)
- **Bidirectional Control**: Read telemetry + send commands
- **JSON Protocol**: Consistent format across TCP and WebSocket

### 🛠 **Developer-Friendly**
- **Thread-safe**: Production-ready concurrent access
- **No Dependencies**: WebSocket implementation built-in, no external libraries
- **Graceful Fallbacks**: Interfaces fail independently
- **Environment Config**: Ports and settings via environment variables

## 📖 Documentation

### Quick Reference
- **[Installation Guide](docs/installation.md)** - Step-by-step setup
- **[API Reference](docs/api_reference.md)** - Complete interface documentation
- **[Variables Reference](docs/variables_reference.md)** - All 339+ available variables
- **[JSON Schema](reference/json_schema.json)** - Payload structure

### Learning Resources
- **[DLL Development Tutorial](tutorials/01_dll_basics.md)** - Learn to create Aerofly DLLs
- **[Architecture Deep Dive](tutorials/02_architecture_explained.md)** - How this bridge works
- **[Best Practices Guide](tutorials/03_best_practices.md)** - Professional DLL development

### Examples & Use Cases
- **[C++ Client](examples/cpp_client/)** - Shared memory integration
- **[Python TCP Client](examples/python_tcp/)** - Network telemetry reader
- **[JavaScript WebSocket](examples/javascript_websocket/)** - Web dashboard
- **[Simple Dashboard](examples/simple_dashboard/)** - Complete web app example

## 🌟 Use Cases

### 🎮 **Real-time Dashboards**
Create web or mobile dashboards displaying live flight data:
```javascript
// WebSocket example
const ws = new WebSocket('ws://localhost:8765');
ws.onmessage = (event) => {
    const data = JSON.parse(event.data);
    updateAltitude(data.aircraft.altitude);
    updateSpeed(data.aircraft.indicated_airspeed);
};
```

### 🔧 **Hardware Integration**
Connect physical panels and controls:
```cpp
// Shared memory example (C++)
HANDLE hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, "AeroflyBridgeData");
AeroflyBridgeData* pData = (AeroflyBridgeData*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);
double altitude = pData->aircraft_altitude;
```

### 📱 **Mobile Applications**
Build companion apps for tablets and phones:
```python
# Python TCP example
import socket, json
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('localhost', 12345))
data = json.loads(sock.recv(4096).decode())
print(f"Altitude: {data['aircraft']['altitude']} ft")
```

### 📈 **Data Logging & Analytics**
Record flights for analysis and training:
```json
{
  "aircraft": {
    "altitude": 3500.0,
    "indicated_airspeed": 120.5,
    "magnetic_heading": 090.0
  },
  "timestamp_us": 1640995200000000
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

## 🤝 Contributing

We welcome contributions! This project serves both production users and the learning community:

- **Bug reports** and **feature requests** via [Issues](issues/)
- **Code contributions** via Pull Requests
- **Documentation improvements** always appreciated
- **Example applications** to showcase different use cases

See our [Contributing Guide](CONTRIBUTING.md) for details.

## 📄 License

This project is licensed under the **BSD 3-Clause License** - see the [LICENSE](LICENSE) file for details.

**Key permissions:**
- ✅ Commercial use
- ✅ Modification and distribution
- ✅ Private use
- ✅ Patent use

## 🙏 Acknowledgments

- **Aerofly FS Team** for the excellent flight simulator and SDK
- **Flight simulation community** for inspiration and feedback
- **Open source contributors** who make projects like this possible

## 📞 Support

- **Documentation**: Check our [docs](docs/) and [examples](examples/)
- **Issues**: Report bugs or request features via [GitHub Issues](issues/)
- **Community**: Join discussions in our [community forums](link-to-community)

---

**Ready to connect your applications to Aerofly FS 4?** 
Start with our [Installation Guide](docs/installation.md) or dive into the [examples](examples/)!