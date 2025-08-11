# DLL Development Basics

**Complete guide to creating external DLLs for Aerofly FS 4**

Copyright (c) 2025 Juan Luis Gabriel

This tutorial teaches you everything needed to build your own external DLL for Aerofly FS 4, from basic concepts to a working implementation. Perfect for developers who want to create custom integrations or understand how the Aerofly Bridge works internally.

## 🎯 What You'll Learn

- **DLL fundamentals** for flight simulators
- **Aerofly SDK** structure and message system
- **DLL lifecycle** and proper initialization
- **Message handling** patterns
- **Build and deployment** process
- **Debugging techniques** for DLL development

## 📚 Prerequisites

**Required knowledge:**
- **C++ basics** (classes, pointers, references)
- **Windows programming** (basic understanding)
- **Visual Studio** familiarity

**Development environment:**
- **Visual Studio 2022** (Community or higher)
- **Windows SDK** (latest)
- **Aerofly FS 4** installed

---

## 🔍 Understanding DLLs in Flight Simulation

### What is a Flight Simulator DLL?

A **Dynamic Link Library (DLL)** for flight simulators is a plugin that extends the simulator's functionality. Unlike standalone applications, DLLs:

- **Run inside the simulator process** (same memory space)
- **Share data directly** with the simulator
- **Execute on the simulator's main thread** (by default)
- **Have access to internal simulator state**

### Why Use DLLs?

**Advantages:**
- **Ultra-low latency** - Direct memory access
- **High performance** - No inter-process communication overhead
- **Real-time integration** - Synchronized with simulation loop
- **Complete access** - All simulator data available

**Common use cases:**
- **Hardware integration** (flight controls, panels)
- **Data logging** and analysis
- **Custom autopilot** systems
- **External gauges** and displays
- **Network bridges** (like our Aerofly Bridge)

### DLL vs External Applications

| Aspect | DLL Plugin | External Application |
|--------|------------|---------------------|
| **Performance** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |
| **Latency** | ~1µs | ~1ms+ |
| **Complexity** | Higher | Lower |
| **Safety** | Can crash sim | Isolated |
| **Debugging** | More difficult | Easier |
| **Distribution** | Single file | Multiple files |

---

## 🧩 Aerofly FS 4 DLL Architecture

### The Aerofly SDK

Aerofly FS 4 provides a **message-based SDK** through the header file `tm_external_message.h`. This header contains:

```cpp
// Core types for the SDK
using tm_uint8 = unsigned char;
using tm_uint32 = unsigned int;
using tm_double = double;

// Message data types
enum class tm_msg_data_type : tm_uint8 {
    None,
    Int,
    Double,
    Vector2d,
    Vector3d,
    String,
    // ... more types
};

// Main message class
class tm_external_message {
    // Message handling functionality
    tm_msg_data_type GetDataType() const;
    double GetDouble() const;
    tm_vector3d GetVector3d() const;
    void SetValue(double value);
    // ... more methods
};
```

### Message-Based Communication

Aerofly communicates with DLLs through **structured messages**:

```cpp
// Each message represents a simulator variable
tm_external_message altitude_msg("Aircraft.Altitude", tm_msg_data_type::Double);
tm_external_message heading_msg("Aircraft.TrueHeading", tm_msg_data_type::Double);
tm_external_message position_msg("Aircraft.Position", tm_msg_data_type::Vector3d);
```

**Message flow:**
```
Aerofly → DLL: "Here's the current aircraft altitude: 3500.0 feet"
DLL → Aerofly: "Set throttle to 75%"
```

### Variable Types and Access

**Scalar values (most common):**
```cpp
// Read altitude (Double)
double altitude = message.GetDouble();

// Read boolean as Double (0.0 or 1.0)
bool onGround = (message.GetDouble() == 1.0);
```

**Vector values:**
```cpp
// Read 3D position (Vector3d)
tm_vector3d position = message.GetVector3d();
double x = position.x;
double y = position.y;
double z = position.z;

// Read 2D coordinates (Vector2d)
tm_vector2d latlon = message.GetVector2d();
double latitude = latlon.x;
double longitude = latlon.y;
```

**String values:**
```cpp
// Read text data (String/String8)
tm_string nav_id = message.GetString();
std::string identifier = nav_id.c_str();
```

---

## 🏗 DLL Structure and Lifecycle

### Required Exports

Every Aerofly DLL **must export** these specific functions:

```cpp
extern "C" {
    // Called once when DLL loads
    __declspec(dllexport) void Aerofly_FS_4_External_DLL_OnLoad();
    
    // Called every simulation frame
    __declspec(dllexport) void Aerofly_FS_4_External_DLL_Update(
        const tm_double delta_time,
        const tm_uint8* const message_list_received_byte_stream,
        const tm_uint32 message_list_received_byte_stream_size,
        const tm_uint32 message_list_received_num_messages,
        tm_uint8* message_list_sent_byte_stream,
        tm_uint32& message_list_sent_byte_stream_size,
        tm_uint32& message_list_sent_num_messages,
        const tm_uint32 message_list_sent_byte_stream_size_max
    );
    
    // Called when DLL unloads
    __declspec(dllexport) void Aerofly_FS_4_External_DLL_OnUnload();
}
```

### DLL Lifecycle

**1. Loading phase:**
```cpp
void Aerofly_FS_4_External_DLL_OnLoad() {
    // Initialize your systems here
    // - Allocate resources
    // - Set up connections
    // - Initialize variables
    
    OutputDebugStringA("My DLL loaded successfully\n");
}
```

**2. Runtime phase (called 50+ times per second):**
```cpp
void Aerofly_FS_4_External_DLL_Update(/* parameters */) {
    // This runs every simulation frame
    // - Process incoming messages
    // - Update your logic
    // - Send commands back to Aerofly
}
```

**3. Unloading phase:**
```cpp
void Aerofly_FS_4_External_DLL_OnUnload() {
    // Clean up resources
    // - Close connections
    // - Free memory
    // - Save settings
    
    OutputDebugStringA("My DLL unloaded cleanly\n");
}
```

### Critical Performance Notes

⚠️ **The Update() function is called from Aerofly's main thread**
- **Never block** for more than a few milliseconds
- **Avoid heavy computations** in this function
- **Use separate threads** for network I/O or file operations
- **Keep it fast** - you're directly affecting simulation smoothness

---

## 📝 Building Your First DLL

### Step 1: Project Setup

**Create a new Visual Studio project:**

1. **File → New → Project**
2. **Visual C++ → Windows Desktop → Dynamic-Link Library (DLL)**
3. **Name**: `MyAeroflyDLL`
4. **Platform**: x64

**Project configuration:**
```cpp
// In Project Properties:
// General → Target Extension: .dll
// C/C++ → Language Standard: ISO C++17
// C/C++ → Preprocessor → Definitions: WIN32;_WINDOWS;_USRDLL
// Linker → System → SubSystem: Windows
```

### Step 2: Include Aerofly SDK

**Download `tm_external_message.h`** from the Aerofly SDK and place it in your project folder.

**In your main source file:**
```cpp
#include <windows.h>
#include <string>
#include <iostream>
#include "tm_external_message.h"

// For debug output
#define DEBUG_OUTPUT(msg) OutputDebugStringA(msg)
```

### Step 3: Implement Basic DLL

**Complete minimal DLL implementation:**

```cpp
#include <windows.h>
#include <vector>
#include <string>
#include "tm_external_message.h"

// Global state
static bool g_initialized = false;
static double g_current_altitude = 0.0;
static double g_current_airspeed = 0.0;

// Message definitions (must match Aerofly's MESSAGE_LIST)
static tm_external_message MessageAircraftAltitude("Aircraft.Altitude", 
    tm_msg_data_type::Double, tm_msg_flag::Value, tm_msg_access::Read);
static tm_external_message MessageAircraftThrottle("Aircraft.Throttle", 
    tm_msg_data_type::Double, tm_msg_flag::Value, tm_msg_access::Write);

extern "C" {
    
__declspec(dllexport) void Aerofly_FS_4_External_DLL_OnLoad() {
    OutputDebugStringA("=== My First Aerofly DLL - Loading ===\n");
    
    // Initialize your systems
    g_initialized = true;
    g_current_altitude = 0.0;
    g_current_airspeed = 0.0;
    
    OutputDebugStringA("DLL initialization complete\n");
}

__declspec(dllexport) void Aerofly_FS_4_External_DLL_Update(
    const tm_double delta_time,
    const tm_uint8* const message_list_received_byte_stream,
    const tm_uint32 message_list_received_byte_stream_size,
    const tm_uint32 message_list_received_num_messages,
    tm_uint8* message_list_sent_byte_stream,
    tm_uint32& message_list_sent_byte_stream_size,
    tm_uint32& message_list_sent_num_messages,
    const tm_uint32 message_list_sent_byte_stream_size_max) {
    
    if (!g_initialized) return;
    
    // === STEP 1: Parse incoming messages ===
    std::vector<tm_external_message> received_messages;
    tm_uint32 pos = 0;
    
    for (tm_uint32 i = 0; i < message_list_received_num_messages; ++i) {
        auto msg = tm_external_message::GetFromByteStream(
            message_list_received_byte_stream, pos);
        received_messages.push_back(msg);
    }
    
    // === STEP 2: Process each message ===
    for (const auto& message : received_messages) {
        tm_uint64 msg_id = message.GetID();
        
        // Check if this is altitude data
        if (msg_id == MessageAircraftAltitude.GetID()) {
            g_current_altitude = message.GetDouble();
            
            // Debug output every 60 frames (~1 second at 60 FPS)
            static int frame_count = 0;
            if (++frame_count >= 60) {
                std::string debug_msg = "Altitude: " + 
                    std::to_string(g_current_altitude) + " meters\n";
                OutputDebugStringA(debug_msg.c_str());
                frame_count = 0;
            }
        }
    }
    
    // === STEP 3: Send commands back (optional) ===
    std::vector<tm_external_message> sent_messages;
    
    // Example: Auto-throttle based on altitude
    if (g_current_altitude < 1000.0) {
        // Low altitude - increase throttle
        MessageAircraftThrottle.SetValue(0.8);  // 80% throttle
        sent_messages.push_back(MessageAircraftThrottle);
    }
    
    // === STEP 4: Serialize outgoing messages ===
    message_list_sent_byte_stream_size = 0;
    message_list_sent_num_messages = 0;
    
    for (const auto& msg : sent_messages) {
        msg.AddToByteStream(message_list_sent_byte_stream,
                           message_list_sent_byte_stream_size,
                           message_list_sent_num_messages);
    }
}

__declspec(dllexport) void Aerofly_FS_4_External_DLL_OnUnload() {
    OutputDebugStringA("=== My First Aerofly DLL - Unloading ===\n");
    
    // Clean up resources
    g_initialized = false;
    
    OutputDebugStringA("DLL cleanup complete\n");
}

} // extern "C"
```

### Step 4: Build Configuration

**Build settings for release:**
```bash
# Command line build (x64 Native Tools Command Prompt)
cl.exe /LD /EHsc /O2 /DNDEBUG /std:c++17 /DWIN32 /D_WINDOWS /D_USRDLL ^
       MyAeroflyDLL.cpp ^
       /Fe:MyAeroflyDLL.dll ^
       /link kernel32.lib user32.lib

# Clean up temporary files
del *.obj *.exp *.lib
```

**Or create a build script (`build.bat`):**
```batch
@echo off
echo Building My Aerofly DLL...

cl.exe /LD /EHsc /O2 /DNDEBUG /std:c++17 /DWIN32 /D_WINDOWS /D_USRDLL ^
       MyAeroflyDLL.cpp ^
       /Fe:MyAeroflyDLL.dll ^
       /link kernel32.lib user32.lib

if %ERRORLEVEL% EQU 0 (
    echo Build successful!
    echo Generated: MyAeroflyDLL.dll
) else (
    echo Build failed!
)

del *.obj *.exp *.lib 2>nul
pause
```

---

## 🚀 Deployment and Testing

### Installation Process

**1. Locate Aerofly's external DLL folder:**
```
%USERPROFILE%\Documents\Aerofly FS 4\external_dll\
```

**2. Copy your DLL:**
```batch
copy MyAeroflyDLL.dll "%USERPROFILE%\Documents\Aerofly FS 4\external_dll\"
```

**3. Launch Aerofly FS 4**
- The DLL loads automatically when Aerofly starts
- Check DebugView for your debug output

### Debugging Your DLL

**Use DebugView for real-time debugging:**

1. **Download DebugView** from Microsoft Sysinternals
2. **Run DebugView as Administrator**
3. **Enable "Capture Win32" and "Capture Global Win32"**
4. **Launch Aerofly FS 4**
5. **Watch for your debug messages**

**Example debug output:**
```
[1234] === My First Aerofly DLL - Loading ===
[1234] DLL initialization complete
[1234] Altitude: 1247.500000 meters
[1234] Altitude: 1251.200000 meters
[1234] === My First Aerofly DLL - Unloading ===
```

**Common debugging techniques:**
```cpp
// Function entry/exit tracking
void MyFunction() {
    OutputDebugStringA("Entering MyFunction\n");
    
    // Your code here
    
    OutputDebugStringA("Exiting MyFunction\n");
}

// Variable monitoring
void LogVariable(const std::string& name, double value) {
    std::string msg = name + ": " + std::to_string(value) + "\n";
    OutputDebugStringA(msg.c_str());
}

// Error conditions
if (something_wrong) {
    OutputDebugStringA("ERROR: Something went wrong!\n");
}
```

---

## 🔧 Message System Deep Dive

### Understanding Message IDs

Each message has a **unique 64-bit hash ID** based on its name:

```cpp
// These messages have different IDs
tm_external_message msg1("Aircraft.Altitude", ...);      // ID: 0x1234567890ABCDEF
tm_external_message msg2("Aircraft.IndicatedAirspeed", ...); // ID: 0xFEDCBA0987654321

// Compare messages by ID (fast)
if (received_message.GetID() == msg1.GetID()) {
    // This is altitude data
    double altitude = received_message.GetDouble();
}
```

### Message Processing Patterns

**Pattern 1: Simple value extraction**
```cpp
for (const auto& message : received_messages) {
    if (message.GetID() == MessageAircraftAltitude.GetID()) {
        double altitude = message.GetDouble();
        // Process altitude...
    }
    else if (message.GetID() == MessageAircraftHeading.GetID()) {
        double heading = message.GetDouble();
        // Process heading...
    }
}
```

**Pattern 2: Using a hash map for scalability**
```cpp
#include <unordered_map>

// Setup (in OnLoad)
std::unordered_map<tm_uint64, std::function<void(const tm_external_message&)>> handlers;

handlers[MessageAircraftAltitude.GetID()] = [](const tm_external_message& msg) {
    double altitude = msg.GetDouble();
    OutputDebugStringA(("Altitude: " + std::to_string(altitude) + "\n").c_str());
};

handlers[MessageAircraftHeading.GetID()] = [](const tm_external_message& msg) {
    double heading = msg.GetDouble();
    OutputDebugStringA(("Heading: " + std::to_string(heading) + "\n").c_str());
};

// Processing (in Update)
for (const auto& message : received_messages) {
    auto it = handlers.find(message.GetID());
    if (it != handlers.end()) {
        it->second(message);
    }
}
```

### Creating and Sending Messages

**Send commands back to Aerofly:**
```cpp
std::vector<tm_external_message> commands_to_send;

// Set throttle to 75%
MessageAircraftThrottle.SetValue(0.75);
commands_to_send.push_back(MessageAircraftThrottle);

// Set flaps to 50%
MessageAircraftFlaps.SetValue(0.5);
commands_to_send.push_back(MessageAircraftFlaps);

// Set heading bug to 090°
MessageAutopilotSelectedHeading.SetValue(90.0);
commands_to_send.push_back(MessageAutopilotSelectedHeading);

// Serialize all commands
for (const auto& cmd : commands_to_send) {
    cmd.AddToByteStream(message_list_sent_byte_stream,
                       message_list_sent_byte_stream_size,
                       message_list_sent_num_messages);
}
```

---

## ⚠️ Common Pitfalls and Solutions

### Pitfall 1: Blocking Operations

**❌ Wrong - will freeze Aerofly:**
```cpp
void Aerofly_FS_4_External_DLL_Update(/* params */) {
    // DON'T DO THIS - blocks simulation!
    Sleep(100);  // 100ms delay = simulation freeze
    
    // DON'T DO THIS - synchronous network call
    std::string response = http_get("http://api.example.com/data");
}
```

**✅ Correct - non-blocking approach:**
```cpp
#include <thread>
#include <atomic>

static std::atomic<bool> g_data_ready{false};
static std::string g_latest_data;

void Aerofly_FS_4_External_DLL_Update(/* params */) {
    // Quick check for new data
    if (g_data_ready.load()) {
        // Process g_latest_data
        g_data_ready.store(false);
    }
    
    // All processing stays fast
}

// Separate thread for slow operations
void background_worker() {
    while (running) {
        std::string data = http_get("http://api.example.com/data");
        g_latest_data = data;
        g_data_ready.store(true);
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
```

### Pitfall 2: Memory Management

**❌ Wrong - memory leaks:**
```cpp
void Aerofly_FS_4_External_DLL_Update(/* params */) {
    // DON'T DO THIS - memory leak every frame!
    double* data = new double[1000];
    // ... use data ...
    // Missing: delete[] data;
}
```

**✅ Correct - proper resource management:**
```cpp
#include <memory>
#include <vector>

void Aerofly_FS_4_External_DLL_Update(/* params */) {
    // Use automatic memory management
    std::vector<double> data(1000);  // Automatic cleanup
    
    // Or use smart pointers when needed
    auto ptr = std::make_unique<SomeObject>();
    // Automatic cleanup when function exits
}
```

### Pitfall 3: Exception Handling

**❌ Wrong - unhandled exceptions crash Aerofly:**
```cpp
void Aerofly_FS_4_External_DLL_Update(/* params */) {
    // DON'T DO THIS - exception crashes simulator!
    double altitude = message.GetDouble();  // Might throw
    double result = 1.0 / altitude;         // Division by zero!
}
```

**✅ Correct - defensive programming:**
```cpp
void Aerofly_FS_4_External_DLL_Update(/* params */) {
    try {
        // Always wrap risky code
        double altitude = message.GetDouble();
        
        if (altitude != 0.0) {  // Check for edge cases
            double result = 1.0 / altitude;
            // Use result...
        }
    }
    catch (const std::exception& e) {
        OutputDebugStringA(("ERROR: " + std::string(e.what()) + "\n").c_str());
    }
    catch (...) {
        OutputDebugStringA("ERROR: Unknown exception caught\n");
    }
}
```

---

<!-- Removed "Next Steps" section with cross-doc links to avoid broken references in public repo. -->

## 📚 Resources and References

### Essential Documentation

- **Aerofly FS 4 SDK** - Check the `tm_external_message.h` header comments
- **Visual Studio Docs** - [C++ DLL development guide](https://docs.microsoft.com/cpp/build/dlls-in-visual-cpp)
- **Windows API** - [DLL programming reference](https://docs.microsoft.com/windows/win32/dlls/dynamic-link-libraries)

### Development Tools

- **DebugView** - [Microsoft Sysinternals](https://docs.microsoft.com/sysinternals/downloads/debugview)
- **Visual Studio** - [Community Edition (free)](https://visualstudio.microsoft.com/vs/community/)
- **Windows SDK** - [Latest version](https://developer.microsoft.com/windows/downloads/windows-10-sdk/)

### Community Resources

- **Aerofly Forums** - Official support and community DLLs
- **GitHub Examples** - Open source DLL projects
- **Flight Sim Developer Communities** - Discord, Reddit, specialized forums

---

**Ready to build your first DLL?** Start with the basic example above, then explore the advanced tutorials to learn professional development patterns used in the Aerofly Bridge implementation.
<!-- Removed next-doc reference to avoid broken links in public repo. -->