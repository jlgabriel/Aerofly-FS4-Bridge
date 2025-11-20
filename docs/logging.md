# Logging System - Aerofly FS4 Bridge

## Overview

The Aerofly FS4 Bridge uses **spdlog** for structured, high-performance logging. The logging system provides multiple log levels, file and console output, and is configurable via environment variables.

## Log Levels

The system supports six log levels:

| Level    | Purpose | Example |
|----------|---------|---------|
| `TRACE`  | Extremely detailed debugging (Debug builds only) | Function entry/exit |
| `DEBUG`  | Development information (Debug builds only) | State changes, detailed flow |
| `INFO`   | Important events | Server started, client connected |
| `WARN`   | Potential issues | Missing configuration, deprecated usage |
| `ERROR`  | Errors that affect functionality | Connection failed, parse error |
| `CRITICAL` | Fatal errors | Initialization failure, unrecoverable errors |

## Configuration

### Environment Variables

Control logging behavior with these environment variables:

```powershell
# Set log level (trace|debug|info|warn|error|critical)
$env:AEROFLY_BRIDGE_LOG_LEVEL = "debug"

# Enable/disable file logging (0|1)
$env:AEROFLY_BRIDGE_LOG_FILE = "1"

# Enable/disable console logging (0|1)
$env:AEROFLY_BRIDGE_LOG_CONSOLE = "1"
```

**Default values:**
- Log level: `INFO` (Release), `DEBUG` (Debug builds)
- File logging: Enabled
- Console logging: Enabled

### Log Files

Logs are written to:
```
%USERPROFILE%\Documents\Aerofly FS 4\logs\bridge_YYYYMMDD.log
```

Example: `C:\Users\YourName\Documents\Aerofly FS 4\logs\bridge_20250820.log`

**File rotation:**
- Maximum file size: 5 MB
- Files kept: 3 (current + 2 rotated)
- Automatic rotation when size limit is reached

## Usage in Code

### Basic Logging

```cpp
#include "logging/logger.h"

// In initialization
Logger::Initialize();

// Throughout code
LOG_INFO("Server started on port {}", port);
LOG_DEBUG("Processing message: {}", msg);
LOG_WARN("Client count ({}) exceeds recommended limit", count);
LOG_ERROR("Failed to bind socket: error {}", WSAGetLastError());
LOG_CRITICAL("Unrecoverable error in component {}", component_name);

// On shutdown
Logger::Shutdown();
```

### Format Strings

spdlog uses Python-style format strings:

```cpp
LOG_INFO("Simple message");
LOG_INFO("Value: {}", 42);
LOG_INFO("Multiple: {}, {}", value1, value2);
LOG_INFO("Named: {name} = {value}", "name"_a = "Altitude", "value"_a = 1000);
LOG_INFO("Hex: 0x{:X}", 255);  // 0xFF
LOG_INFO("Float: {:.2f}", 3.14159);  // 3.14
```

### Conditional Logging

```cpp
// Only log in debug builds
#if !defined(NDEBUG)
    LOG_DEBUG("Debug-only message");
#endif

// Log with condition
if (error_occurred) {
    LOG_ERROR("Error details: {}", error_message);
}
```

### Force Flush

```cpp
// Force write pending logs (before critical operation)
LOG_FLUSH();

// Or directly
Logger::Flush();
```

## Log Output Format

### Console (Debug Output)

```
[14:32:11.456] [INFO] [thread 12345] Server started on port 12345
[14:32:11.789] [DEBUG] [thread 67890] Client connected from 127.0.0.1:54321
[14:32:12.001] [ERROR] [thread 67890] Frame parse error - invalid opcode
```

### File

```
[2025-08-20 14:32:11.456] [info] [thread 12345] Server started on port 12345
[2025-08-20 14:32:11.789] [debug] [thread 67890] Client connected from 127.0.0.1:54321
[2025-08-20 14:32:12.001] [error] [thread 67890] Frame parse error - invalid opcode
```

## Performance

### Asynchronous Logging

- Logs are buffered in memory
- Background thread writes to disk
- No blocking of main threads
- Automatic flush every 3 seconds
- Immediate flush on WARNING or higher

### Overhead

- LOG_TRACE and LOG_DEBUG have zero cost in Release builds (compiled out)
- LOG_INFO and above: ~1-2 microseconds per call (async mode)

## Viewing Logs

### During Development

**Visual Studio:**
1. Run Aerofly FS4 with debugger attached
2. View → Output → Show output from: Debug
3. Logs appear in real-time

**DebugView (Sysinternals):**
1. Download from Microsoft Sysinternals
2. Run DebugView.exe as Administrator
3. Capture → Capture Global Win32

### After Flight

Open log file:
```powershell
notepad "$env:USERPROFILE\Documents\Aerofly FS 4\logs\bridge_$(Get-Date -Format 'yyyyMMdd').log"
```

Or use a log viewer:
- [Notepad++](https://notepad-plus-plus.org/) with syntax highlighting
- [Baretail](https://www.baremetalsoft.com/baretail/) for live tailing
- [LogExpert](https://github.com/zarunbal/LogExpert) for advanced filtering

## Troubleshooting

### No Logs Appearing

1. **Check environment variables:**
   ```powershell
   Get-ChildItem Env:AEROFLY_BRIDGE_*
   ```

2. **Verify logger initialization:**
   - Should see "Logging system initialized" message
   - Check if Logger::Initialize() is called

3. **Check file permissions:**
   - Ensure Documents folder is writable
   - Check antivirus isn't blocking file creation

### Logs Directory Not Created

The logger automatically creates the logs directory. If it fails:

```powershell
# Manually create directory
mkdir "$env:USERPROFILE\Documents\Aerofly FS 4\logs"
```

### Too Many/Few Logs

```powershell
# Reduce verbosity
$env:AEROFLY_BRIDGE_LOG_LEVEL = "info"

# Increase verbosity
$env:AEROFLY_BRIDGE_LOG_LEVEL = "debug"

# Maximum verbosity (debug builds only)
$env:AEROFLY_BRIDGE_LOG_LEVEL = "trace"
```

## Best Practices

1. **Use appropriate levels:**
   - INFO for user-visible events
   - DEBUG for developer information
   - ERROR for actual problems

2. **Include context:**
   ```cpp
   LOG_ERROR("Failed to connect to {}:{} - error {}", host, port, err);
   ```

3. **Avoid logging in tight loops:**
   ```cpp
   // Bad: Logs thousands of times per second
   for (auto& client : clients) {
       LOG_DEBUG("Processing client {}", client.id);
   }

   // Good: Log summary
   LOG_DEBUG("Processing {} clients", clients.size());
   ```

4. **Log important state changes:**
   ```cpp
   LOG_INFO("TCP server started on ports {} and {}", data_port, cmd_port);
   LOG_INFO("WebSocket server started on port {}", ws_port);
   LOG_INFO("Shared memory initialized: {} bytes", size);
   ```

5. **Flush before critical sections:**
   ```cpp
   LOG_CRITICAL("Fatal error occurred");
   LOG_FLUSH();  // Ensure log is written before potential crash
   ```

## Migration from DBG() Macro

Old code:
```cpp
DBG("TCP server started\n");
OutputDebugStringA("Client connected\n");
```

New code:
```cpp
LOG_INFO("TCP server started on port {}", port);
LOG_DEBUG("Client connected from {}", address);
```

## Advanced Configuration

### Custom Log Patterns

Edit `src/logging/logger.cpp` to change log format:

```cpp
// Console pattern
console_sink->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

// File pattern
file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");
```

Pattern flags:
- `%Y-%m-%d` - Date
- `%H:%M:%S.%e` - Time with milliseconds
- `%l` - Log level
- `%t` - Thread ID
- `%v` - Message
- `%^%l%$` - Colored log level

### Additional Sinks

Add new output destinations in logger.cpp:

```cpp
// Add Windows Event Log sink
auto event_sink = std::make_shared<spdlog::sinks::win_eventlog_sink_mt>("AeroflyBridge");
sinks.push_back(event_sink);

// Add network sink (for centralized logging)
// Requires additional spdlog sink implementation
```

## References

- [spdlog GitHub](https://github.com/gabime/spdlog)
- [spdlog Wiki](https://github.com/gabime/spdlog/wiki)
- [Format String Syntax](https://fmt.dev/latest/syntax.html)
