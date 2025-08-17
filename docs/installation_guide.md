<!-- Content migrated to the new filename. -->

# Installation Guide

**Complete setup guide for Aerofly FS4 Bridge DLL**

Copyright (c) 2025 Juan Luis Gabriel

This guide covers everything you need to get the bridge running, from basic installation to advanced development setup.

## 📋 Prerequisites

### System Requirements

**Operating System:**
- Windows 10 (version 1903 or later) ✅
- Windows 11 (all versions) ✅

**Aerofly FS 4:**
- Any version with external DLL support
- Make sure Aerofly FS 4 is installed and working properly

**Aerofly SDK Header (required for building from source):**
- Download `tm_external_message.h` from the official Aerofly developers page (`https://www.aerofly.com/developers/`), section "External DLL".
- Place the file at the project root or inside the `src/` folder before compiling.

**Hardware:**
- **RAM**: Minimum 8GB (16GB recommended for development)
- **CPU**: Any 64-bit processor (Intel/AMD)
- **Storage**: ~50MB free space
- **Network**: Optional (only needed for TCP/WebSocket features)

### For Users (Ready-to-Use Installation)

**Requirements:**
- Aerofly FS 4 installed
- Administrator privileges (for DLL installation)

### For Developers (Building from Source)

**Requirements:**
- **Visual Studio 2022** (Community, Professional, or Enterprise)
- **Windows SDK** (latest version)
- **Git** (optional, for cloning repository)

**Visual Studio Components Required:**
- Desktop development with C++
- Windows 10/11 SDK
- MSVC v143 - VS 2022 C++ x64/x86 build tools

---

## 🚀 Quick Installation (Users)

### Step 1: Download the DLL

1. Go to [GitHub Releases](https://github.com/jlgabriel/Aerofly-FS4-Bridge/releases)
2. Download the latest `AeroflyBridge.dll`
3. Save it to a temporary location (like Desktop)

### Step 2: Locate Aerofly FS 4 Directory

Find your Aerofly FS 4 external DLL folder:

```
%USERPROFILE%\Documents\Aerofly FS 4\external_dll\
```

**To navigate there quickly:**

1. Press `Win + R` to open Run dialog
2. Type: `%USERPROFILE%\Documents\Aerofly FS 4\external_dll`
3. Press Enter

**If the folder doesn't exist:**
- Create the `external_dll` folder manually in your Aerofly FS 4 documents directory

### Step 3: Install the DLL

1. **Backup existing DLL** (if you have one):
   ```cmd
   copy AeroflyBridge.dll AeroflyBridge_backup.dll
   ```

2. **Copy the new DLL**:
   - Copy `AeroflyBridge.dll` to the `external_dll` folder
   - **Important**: The file must be named exactly `AeroflyBridge.dll`

### Step 4: Verify Installation

1. **Launch Aerofly FS 4**
2. **Load any aircraft** and start a flight
3. **Check that it loads without crashes**

**Success indicators:**
- Aerofly FS 4 starts normally
- No error messages about DLL loading
- Flight loads successfully

---

## ✅ Quick Verification Test

Once installed, verify that the bridge is working:

### Test 1: Check Shared Memory

**Using Command Prompt:**

1. Open Command Prompt as Administrator
2. Run this PowerShell command:
   ```powershell
   powershell -Command "Get-Process | Where-Object {$_.ProcessName -eq 'AeroflyFS4'}"
   ```
3. If Aerofly is running, the bridge should be active

### Test 2: Check Network Interfaces

**Test TCP Connection:**

1. Open Command Prompt
2. Test if TCP server is listening:
   ```cmd
   netstat -an | findstr :12345
   ```
3. You should see: `TCP    0.0.0.0:12345    0.0.0.0:0    LISTENING`

**Test WebSocket Connection:**

1. Test WebSocket server:
   ```cmd
   netstat -an | findstr :8765
   ```
2. You should see: `TCP    0.0.0.0:8765    0.0.0.0:0    LISTENING`

### Test 3: Simple Data Test

**Using telnet (TCP test):**

1. Install telnet if needed:
   ```cmd
   dism /online /Enable-Feature /FeatureName:TelnetClient
   ```

2. Connect to data stream:
   ```cmd
   telnet localhost 12345
   ```

3. You should see JSON data streaming like:
   ```json
   {
     "schema": "aerofly-bridge-telemetry",
     "schema_version": 1,
     "timestamp": 1640995200000000,
     "timestamp_unit": "microseconds",
     "data_valid": 1,
     "broadcast_rate_hz": 50.0,
     "variables": {
       "Aircraft.Altitude": 1000.0,
       "Aircraft.IndicatedAirspeed": 120.5
     }
   }
   ```

---

## 🔧 Configuration (Optional)

The bridge works out-of-the-box, but you can customize it with environment variables:

### Environment Variables

**To set environment variables:**

1. Right-click "This PC" → Properties
2. Click "Advanced system settings"
3. Click "Environment Variables"
4. Click "New" under User variables

**Available options:**

| Variable | Default | Description |
|----------|---------|-------------|
| `AEROFLY_BRIDGE_WS_ENABLE` | `1` | Enable WebSocket server (1=on, 0=off) |
| `AEROFLY_BRIDGE_WS_PORT` | `8765` | WebSocket server port |
| `AEROFLY_BRIDGE_BROADCAST_MS` | `20` | Data broadcast interval (milliseconds) |

**Example configuration:**
```
AEROFLY_BRIDGE_WS_ENABLE=1
AEROFLY_BRIDGE_WS_PORT=9000
AEROFLY_BRIDGE_BROADCAST_MS=50
```

**After changing variables:**
- Restart Aerofly FS 4 for changes to take effect

---

## ⚠️ Common Issues

### Issue 1: DLL Not Loading

**Symptoms:**
- Aerofly crashes on startup
- Error about missing DLL

**Solutions:**
1. **Check file location**: DLL must be in `external_dll` folder
2. **Check filename**: Must be exactly `AeroflyBridge.dll`
3. **Check permissions**: Run Aerofly as Administrator once
4. **Missing dependencies**: Reinstall Visual C++ Redistributable 2022

### Issue 2: Network Ports Blocked

**Symptoms:**
- Can't connect via TCP/WebSocket
- Ports show as not listening

**Solutions:**
1. **Check Windows Firewall**: Allow Aerofly FS 4 through firewall
2. **Check antivirus**: Temporarily disable real-time protection
3. **Check port conflicts**: Use `netstat -an` to see what's using ports 12345, 12346, 8765

### Issue 3: No Data Streaming

**Symptoms:**
- Connects but no JSON data received
- Empty responses

**Solutions:**
1. **Start a flight**: Bridge only sends data during active flight
2. **Load an aircraft**: Some data requires aircraft to be loaded
3. **Check environment variables**: Verify broadcast interval isn't too high


---

## 🛠 Building from Source (Developers)

*This section covers building the DLL yourself for development or customization.*

### Development Environment Setup

1. **Install Visual Studio 2022**:
   - Download from [Microsoft's website](https://visualstudio.microsoft.com/)
   - Choose Community (free) or Professional/Enterprise
   - During installation, select "Desktop development with C++"

2. **Install Windows SDK**:
   - Usually included with Visual Studio
   - Or download separately from Microsoft

3. **Clone the repository**:
   ```bash
   git clone https://github.com/yourusername/Aerofly-FS4-Bridge.git
   cd Aerofly-FS4-Bridge/src
   ```

### Building the DLL

**Method 1: Using the Build Script (Recommended)**

1. Open Command Prompt in the `src` folder
2. Run the build script:
   ```cmd
   compile.bat
   ```
3. The script will:
   - Auto-detect Visual Studio 2022
   - Set up the build environment
   - Compile with optimized settings
   - Output `AeroflyBridge.dll`

**Method 2: Manual Compilation**

1. Open "x64 Native Tools Command Prompt for VS 2022"
2. Navigate to the `src` folder
3. Run the compile command:
   ```cmd
   cl.exe /LD /EHsc /O2 /DNDEBUG /std:c++17 /DWIN32 /D_WINDOWS /D_USRDLL aerofly_bridge_dll_complete_estable.cpp /Fe:AeroflyBridge.dll /link ws2_32.lib kernel32.lib user32.lib
   ```

### Important Build Notes

**Required Files:**
- `aerofly_bridge_dll_complete_estable.cpp` (main source)
- `tm_external_message.h` (Aerofly SDK header)

**Build Flags Explained:**
- `/DNDEBUG`: Disables assert() macros (prevents Windows 10 breakpoint issues)
- `/O2`: Full optimization for release build
- `/std:c++17`: Uses C++17 standard
- `/LD`: Creates a DLL instead of executable

**Output Files:**
- `AeroflyBridge.dll` - The final bridge DLL
- Temporary files (`.obj`, `.exp`, `.lib`) are automatically cleaned

### Development Testing

**After building:**

1. **Copy to Aerofly**: Follow the installation steps above
2. **Test thoroughly**: Verify all three interfaces work
3. **Check for memory leaks**: Use Visual Studio diagnostics
4. **Performance testing**: Monitor CPU/memory usage

**Debug Build (for development):**
Use Visual Studio or adapt `scripts\compile.bat` replacing `/O2 /DNDEBUG` por `/Od /Zi` y cambiando `/Fe:dist\AeroflyBridge.dll` por `/Fe:dist\AeroflyBridge_debug.dll`.

---

<!-- Removed cross-document next steps to avoid broken links in public repo. -->