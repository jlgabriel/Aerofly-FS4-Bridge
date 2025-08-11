# Architecture Deep Dive

**Understanding the multi-interface design of Aerofly FS4 Bridge**

Copyright (c) 2025 Juan Luis Gabriel

This tutorial explores the advanced architectural patterns used in the Aerofly Bridge, teaching you how to design scalable, high-performance flight simulation applications. Learn the thinking behind the multi-interface approach and how to implement similar systems.

## 🎯 What You'll Learn

- **Multi-interface architecture** design principles
- **Threading patterns** for real-time systems
- **Data flow optimization** techniques
- **Component isolation** strategies
- **Performance vs complexity** trade-offs
- **Scalability patterns** for flight sim apps

## 📋 Prerequisites

- **C++ intermediate** knowledge (templates, RAII, STL)
- **Multi-threading concepts** (basic understanding)
- **Network programming** awareness

---

## 🏗 Overall System Architecture

### The Big Picture

The Aerofly Bridge implements a **hub-and-spoke architecture** with the DLL as the central hub connecting multiple interfaces:

```
                    ┌─────────────────┐
                    │   Aerofly FS4   │
                    │   (Main Thread) │
                    └─────────┬───────┘
                              │ Message API
                              ▼
                    ┌─────────────────┐
                    │  AeroflyBridge  │◄─── Single DLL Instance
                    │   (Main Class)  │
                    └─────────┬───────┘
                              │
            ┌─────────────────┼─────────────────┐
            ▼                 ▼                 ▼
   ┌───────────────┐ ┌───────────────┐ ┌───────────────┐
   │ SharedMemory  │ │ TCPInterface  │ │ WebSocketIntf │
   │  Interface    │ │ (Thread Pool) │ │ (Thread Pool) │
   └───────┬───────┘ └───────┬───────┘ └───────┬───────┘
           │                 │                 │
           ▼                 ▼                 ▼
   ┌───────────────┐ ┌───────────────┐ ┌───────────────┐
   │ Local Apps    │ │ Network Apps  │ │ Web/Mobile    │
   │ (Ultra-fast)  │ │ (Cross-host)  │ │ (Browser)     │
   └───────────────┘ └───────────────┘ └───────────────┘
```

### Design Philosophy

**1. Interface Isolation**
- Each interface is **completely independent**
- Failure of one doesn't affect others
- Different performance characteristics
- Separate threading models

**2. Single Source of Truth**
- All interfaces read from **same shared memory**
- Consistent data across all clients
- No data synchronization issues
- Atomic updates ensure consistency

**3. Graceful Degradation**
- TCP failure → Shared Memory still works
- WebSocket disabled → TCP continues normally
- Network issues → Local apps unaffected

---

## 🔄 Data Flow Architecture

### Primary Data Flow

```cpp
// Simplified data flow representation
Aerofly SDK → Update() → ProcessMessages() → SharedMemory → BuildJSON()
                                                    ↓
                                            BroadcastToClients()
                                                    ↓
                                     ┌──────────────────┐
                                     ▼                  ▼
                                TCP Clients    WebSocket Clients
```

### Detailed Flow Analysis

**1. Ingestion Phase (Aerofly → Bridge)**
```cpp
void AeroflyBridge::Update(
    const std::vector<tm_external_message>& received_messages,
    double delta_time,
    std::vector<tm_external_message>& sent_messages) {
    
    // CRITICAL: This runs on Aerofly's main thread
    // Must be ultra-fast (< 1ms typical)
    
    // Update shared memory with latest data
    shared_memory.UpdateData(received_messages, delta_time);
    
    // Non-blocking broadcasts to network interfaces
    if (tcp_server.GetClientCount() > 0) {
        tcp_server.BroadcastData(shared_memory.GetData());
    }
    
    if (ws_enabled && ws_server.GetClientCount() > 0) {
        ws_server.BroadcastData(shared_memory.GetData());
    }
}
```

**2. Processing Phase (Background threads)**
```cpp
// TCP server thread (runs independently)
void TCPServerInterface::ServerLoop() {
    while (running) {
        // Accept new connections (non-blocking)
        SOCKET client = accept(server_socket, nullptr, nullptr);
        
        if (client != INVALID_SOCKET) {
            // Add to client list (thread-safe)
            std::lock_guard<std::mutex> lock(clients_mutex);
            client_sockets.push_back(client);
        }
        
        // Broadcast happens separately in BroadcastData()
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
```

**3. Output Phase (Multi-threaded broadcasts)**
```cpp
// Each interface handles its own broadcasting
void TCPServerInterface::BroadcastData(const AeroflyBridgeData* data) {
    // Throttling: respect AEROFLY_BRIDGE_BROADCAST_MS
    if (!should_broadcast_now()) return;
    
    // Build JSON once (expensive operation)
    const std::string json = BuildDataJSON(data);
    
    // Send to all clients (parallel-friendly)
    std::vector<SOCKET> clients_snapshot;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients_snapshot = client_sockets;  // Copy for thread safety
    }
    
    // Broadcast without holding locks
    for (SOCKET client : clients_snapshot) {
        send_non_blocking(client, json);
    }
}
```

---

## 🧵 Threading Model Deep Dive

### Thread Responsibilities

The bridge uses a **specialized threading model** optimized for real-time performance:

```cpp
/*
Threading Overview:
┌──────────────────┐    ┌──────────────────┐    ┌──────────────────┐
│ Aerofly Thread   │    │ TCP Thread Pool  │    │ WebSocket Thread │
│ (50-60 Hz)       │    │ (2 threads)      │    │ (1 thread)       │
├──────────────────┤    ├──────────────────┤    ├──────────────────┤
│ • Update()       │    │ • Accept clients │    │ • Accept clients │
│ • ProcessMsgs    │    │ • Handle commands│    │ • WebSocket I/O  │
│ • UpdateSharedMem│    │ • Broadcast data │    │ • Broadcast data │
│ • Fast execution │    │ • Non-blocking   │    │ • RFC 6455       │
└──────────────────┘    └──────────────────┘    └──────────────────┘
         │                        │                        │
         └────────── SharedMemory ─────────────────────────┘
                    (Thread-safe)
*/
```

### Thread 1: Aerofly Main Thread

**Characteristics:**
- **Frequency**: 50-60 Hz (Aerofly's simulation rate)
- **Latency requirement**: < 1ms typically
- **Responsibility**: Core data processing

**Critical implementation details:**
```cpp
void AeroflyBridge::Update(/* ... */) {
    // === PERFORMANCE CRITICAL SECTION ===
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 1. Update shared memory (fast path)
    shared_memory.UpdateData(received_messages, delta_time);
    
    // 2. Non-blocking client count checks
    bool has_tcp_clients = (tcp_server.GetClientCount() > 0);
    bool has_ws_clients = ws_enabled && (ws_server.GetClientCount() > 0);
    
    // 3. Trigger broadcasts (delegate to background threads)
    if (has_tcp_clients) {
        tcp_server.BroadcastData(shared_memory.GetData());
    }
    
    if (has_ws_clients) {
        ws_server.BroadcastData(shared_memory.GetData());
    }
    
    // 4. Process command queues (fast)
    ProcessIncomingCommands(sent_messages);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time);
    
    // Debug: Monitor performance
    if (duration.count() > 1000) {  // > 1ms
        OutputDebugStringA("WARNING: Update() took too long!\n");
    }
}
```

### Thread 2: TCP Server Thread

**Characteristics:**
- **Purpose**: Accept TCP connections
- **Blocking**: Minimal (uses select() with timeouts)
- **Scalability**: Handles 50+ simultaneous clients

```cpp
void TCPServerInterface::ServerLoop() {
    while (running) {
        // Use select() for non-blocking accept
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_socket, &read_fds);
        
        struct timeval timeout = {0, 1000};  // 1ms timeout
        int result = select(0, &read_fds, nullptr, nullptr, &timeout);
        
        if (result > 0 && FD_ISSET(server_socket, &read_fds)) {
            SOCKET client = accept(server_socket, nullptr, nullptr);
            
            if (client != INVALID_SOCKET) {
                // Configure client socket
                set_non_blocking(client);
                
                // Thread-safe client registration
                {
                    std::lock_guard<std::mutex> lock(clients_mutex);
                    client_sockets.push_back(client);
                }
                
                OutputDebugStringA("New TCP client connected\n");
            }
        }
    }
}
```

### Thread 3: TCP Command Thread

**Characteristics:**
- **Purpose**: Receive JSON commands from TCP clients
- **Pattern**: Producer-Consumer with command queue

```cpp
void TCPServerInterface::CommandLoop() {
    while (running) {
        std::vector<SOCKET> clients_to_check;
        
        // Get snapshot of clients
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients_to_check = client_sockets;
        }
        
        // Check each client for incoming data
        for (SOCKET client : clients_to_check) {
            char buffer[1024];
            int bytes_received = recv(client, buffer, sizeof(buffer) - 1, 0);
            
            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';
                std::string command(buffer);
                
                // Thread-safe command queuing
                {
                    std::lock_guard<std::mutex> lock(command_mutex);
                    command_queue.push(command);
                }
            }
            else if (bytes_received == 0 || WSAGetLastError() != WSAEWOULDBLOCK) {
                // Client disconnected
                remove_client(client);
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
```

### Thread 4: WebSocket Server Thread

**Characteristics:**
- **Purpose**: Handle WebSocket protocol
- **Complexity**: RFC 6455 implementation
- **Features**: Handshake + Frame processing

```cpp
void WebSocketServerInterface::ServerLoop() {
    while (running) {
        // Handle both HTTP upgrade and WebSocket frames
        handle_incoming_connections();
        handle_websocket_frames();
        handle_client_disconnections();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void WebSocketServerInterface::handle_websocket_frames() {
    std::vector<SOCKET> clients_snapshot;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients_snapshot = client_sockets;
    }
    
    for (SOCKET client : clients_snapshot) {
        std::string& buffer = recv_buffers[client];
        
        // Read raw data
        char temp_buffer[4096];
        int bytes = recv(client, temp_buffer, sizeof(temp_buffer), 0);
        
        if (bytes > 0) {
            buffer.append(temp_buffer, bytes);
            
            // Parse WebSocket frames
            while (auto frame = parse_websocket_frame(buffer)) {
                if (frame->opcode == 0x1) {  // Text frame
                    process_text_frame(frame->payload);
                }
                else if (frame->opcode == 0x9) {  // Ping
                    send_pong_frame(client);
                }
            }
        }
    }
}
```

---

## 🏭 Component Architecture

### Interface Separation Strategy

Each interface is implemented as a **self-contained component** with clear boundaries:

```cpp
// Base interface pattern
class NetworkInterface {
public:
    virtual ~NetworkInterface() = default;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual void BroadcastData(const AeroflyBridgeData* data) = 0;
    virtual std::vector<std::string> GetPendingCommands() = 0;
    virtual int GetClientCount() const = 0;
};

// Concrete implementations
class TCPServerInterface : public NetworkInterface { /* ... */ };
class WebSocketServerInterface : public NetworkInterface { /* ... */ };
```

### Shared Memory Component

**Design principle**: **Lock-free for readers, exclusive for writers**

```cpp
class SharedMemoryInterface {
private:
    HANDLE hMapFile;
    AeroflyBridgeData* pData;
    mutable std::mutex data_mutex;  // Only for writes
    bool initialized;

public:
    // Fast read access (no locks needed if data_valid == 1)
    const AeroflyBridgeData* GetData() const {
        return pData;  // Atomic pointer read
    }
    
    // Write access (exclusive)
    void UpdateData(const std::vector<tm_external_message>& messages, 
                   double delta_time) {
        std::lock_guard<std::mutex> lock(data_mutex);
        
        // Mark data as updating
        pData->data_valid = 0;
        
        // Update all fields
        process_all_messages(messages);
        update_timestamp();
        
        // Mark data as ready (atomic write)
        pData->data_valid = 1;
    }
};
```

### Command Processing Component

**Design principle**: **Centralized command parsing with distributed execution**

```cpp
class CommandProcessor {
private:
    VariableMapper mapper;  // String name → Index mapping
    
public:
    std::vector<tm_external_message> ProcessCommands(
        const std::vector<std::string>& commands) {
        
        std::vector<tm_external_message> messages;
        
        for (const auto& command : commands) {
            auto msg = ParseCommand(command);
            if (msg.GetDataType() != tm_msg_data_type::None) {
                messages.push_back(msg);
            }
        }
        
        return messages;
    }

private:
    tm_external_message ParseCommand(const std::string& command) {
        // Robust JSON parsing with error handling
        try {
            auto json = parse_json(command);
            std::string var_name = json["variable"];
            double value = json["value"];
            
            // Route to appropriate subsystem
            if (var_name.starts_with("Aircraft.")) {
                return ProcessAircraftVariable(var_name, value);
            }
            else if (var_name.starts_with("Controls.")) {
                return ProcessControlsVariable(var_name, value);
            }
            // ... more subsystems
        }
        catch (const std::exception& e) {
            OutputDebugStringA(("JSON parse error: " + std::string(e.what())).c_str());
        }
        
        return tm_external_message();  // Empty = invalid
    }
};
```

---

## ⚡ Performance Optimization Strategies

### 1. JSON Generation Optimization

**Problem**: JSON generation is expensive (string concatenation)
**Solution**: Single-pass generation with pre-allocated buffers

```cpp
static std::string BuildDataJSON(const AeroflyBridgeData* data) {
    if (!data) return "{}";

    std::ostringstream json;
    json.setf(std::ios::fixed);
    json.precision(6);

    // Single-pass generation following the canonical schema
    json << "{"
         << "\"schema\":\"aerofly-bridge-telemetry\"," 
         << "\"schema_version\":1,"
         << "\"timestamp\":" << data->timestamp_us << ","
         << "\"timestamp_unit\":\"microseconds\"," 
         << "\"data_valid\":" << data->data_valid << ","
         << "\"update_counter\":" << data->update_counter << ","
         // Optional: include broadcast cadence hint
         << "\"broadcast_rate_hz\":50.0,"  // set from env/config in production
         // Canonical Aerofly SDK variable names
         << "\"variables\":{"
         << "\"Aircraft.Altitude\":" << data->aircraft_altitude << ","
         << "\"Aircraft.IndicatedAirspeed\":" << data->aircraft_indicated_airspeed
         << "}"
         // Note: `all_variables` array omitted here for brevity
         << "}";

    return json.str();
}
```

### 2. Broadcast Throttling

**Problem**: High-frequency updates overwhelm network clients
**Solution**: Configurable throttling with frame skipping

```cpp
void TCPServerInterface::BroadcastData(const AeroflyBridgeData* data) {
    // Throttling logic
    const uint64_t now_us = get_time_us();
    static uint64_t last_broadcast_us = 0;
    
    // Read throttle setting once
    static uint32_t interval_us = []() {
        char buf[32] = {0};
        DWORD n = GetEnvironmentVariableA("AEROFLY_BRIDGE_BROADCAST_MS", buf, sizeof(buf));
        int ms = (n > 0) ? atoi(buf) : 20;  // Default 20ms = 50Hz
        return static_cast<uint32_t>(ms * 1000);  // Convert to microseconds
    }();
    
    if (now_us - last_broadcast_us < interval_us) {
        return;  // Skip this frame
    }
    
    last_broadcast_us = now_us;
    
    // Proceed with broadcast
    const std::string json = BuildDataJSON(data);
    broadcast_to_all_clients(json);
}
```

### 3. Memory Layout Optimization

**Problem**: Cache misses from scattered data access
**Solution**: Structured memory layout with hot/cold separation

```cpp
struct AeroflyBridgeData {
    // === HOT DATA (accessed every frame) ===
    uint64_t timestamp_us;              // 8 bytes
    uint32_t data_valid;               // 4 bytes  
    uint32_t update_counter;           // 4 bytes
    
    // Essential flight data (cache-friendly grouping)
    double aircraft_altitude;           // 8 bytes
    double aircraft_indicated_airspeed; // 8 bytes
    double aircraft_pitch;             // 8 bytes
    double aircraft_bank;              // 8 bytes
    // ... other frequently accessed variables
    
    // === WARM DATA (accessed occasionally) ===
    double performance_speeds[10];     // Performance envelope
    double navigation_frequencies[20]; // Radio data
    
    // === COLD DATA (rarely accessed) ===
    char aircraft_name[256];           // String data at end
    char airport_names[512];
    
    // === DIRECT ACCESS ARRAY ===
    double all_variables[400];         // For indexed access
};

static_assert(sizeof(AeroflyBridgeData) < 8192, "Keep under 8KB for cache efficiency");
```

### 4. Lock-Free Patterns

**Problem**: Mutex contention between threads
**Solution**: Lock-free data structures for high-frequency operations

```cpp
class LockFreeCommandQueue {
private:
    std::atomic<size_t> write_index{0};
    std::atomic<size_t> read_index{0};
    std::array<std::string, 1024> commands;  // Ring buffer
    
public:
    bool try_push(const std::string& command) {
        size_t current_write = write_index.load();
        size_t next_write = (current_write + 1) % commands.size();
        
        if (next_write == read_index.load()) {
            return false;  // Queue full
        }
        
        commands[current_write] = command;
        write_index.store(next_write);
        return true;
    }
    
    bool try_pop(std::string& command) {
        size_t current_read = read_index.load();
        
        if (current_read == write_index.load()) {
            return false;  // Queue empty
        }
        
        command = commands[current_read];
        read_index.store((current_read + 1) % commands.size());
        return true;
    }
};
```

---

## 🔧 Error Handling and Resilience

### Graceful Degradation Strategy

```cpp
bool AeroflyBridge::Initialize() {
    // Core component (required)
    if (!shared_memory.Initialize()) {
        OutputDebugStringA("CRITICAL: Shared memory failed - bridge disabled\n");
        return false;
    }
    
    // Optional components (graceful fallback)
    if (!tcp_server.Start(12345, 12346)) {
        OutputDebugStringA("WARNING: TCP server failed - continuing without network\n");
        // Continue initialization - shared memory still works
    }
    
    if (ws_enabled && !ws_server.Start(ws_port)) {
        OutputDebugStringA("WARNING: WebSocket server failed - TCP still available\n");
        ws_enabled = false;  // Disable WebSocket, keep TCP
    }
    
    initialized = true;
    return true;
}
```

### Network Resilience Patterns

```cpp
void TCPServerInterface::HandleClientError(SOCKET client, const std::string& error) {
    OutputDebugStringA(("Client error: " + error + "\n").c_str());
    
    // Remove client gracefully
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = std::find(client_sockets.begin(), client_sockets.end(), client);
        if (it != client_sockets.end()) {
            closesocket(*it);
            client_sockets.erase(it);
        }
    }
    
    // Clear associated buffers
    recv_buffers.erase(client);
    
    // Continue serving other clients
    OutputDebugStringA("Client removed, continuing with remaining clients\n");
}
```

### Resource Cleanup Strategy

```cpp
class RAIICleanup {
private:
    std::function<void()> cleanup_function;
    
public:
    explicit RAIICleanup(std::function<void()> cleanup) 
        : cleanup_function(cleanup) {}
    
    ~RAIICleanup() {
        if (cleanup_function) {
            try {
                cleanup_function();
            }
            catch (...) {
                // Never throw from destructor
                OutputDebugStringA("WARNING: Exception during cleanup\n");
            }
        }
    }
};

void AeroflyBridge::Shutdown() {
    // Ensure cleanup happens even if exceptions occur
    RAIICleanup tcp_cleanup([this]() { tcp_server.Stop(); });
    RAIICleanup ws_cleanup([this]() { ws_server.Stop(); });
    RAIICleanup mem_cleanup([this]() { shared_memory.Cleanup(); });
    
    OutputDebugStringA("Bridge shutdown initiated\n");
    
    // Cleanup happens automatically via RAII
    initialized = false;
    
    OutputDebugStringA("Bridge shutdown completed\n");
}
```

---

## 📊 Scalability Considerations

### Client Connection Scaling

**Current limits and bottlenecks:**

```cpp
/*
Theoretical Limits:
┌─────────────────┬──────────────┬────────────────┐
│ Interface       │ Max Clients  │ Bottleneck     │
├─────────────────┼──────────────┼────────────────┤
│ Shared Memory   │ ~50-100      │ OS handles     │
│ TCP             │ ~50-200      │ JSON generation│
│ WebSocket       │ ~20-50       │ Frame overhead │
└─────────────────┴──────────────┴────────────────┘

Real-world limits (tested):
- TCP: 50+ clients @ 50Hz = stable
- WebSocket: 20+ clients @ 50Hz = stable  
- Shared Memory: Limited by OS, not bridge
*/
```

**Scaling strategies for high client counts:**

```cpp
// Strategy 1: Client pooling with load balancing
class ClientPool {
    std::vector<std::vector<SOCKET>> client_groups;
    std::atomic<size_t> round_robin_index{0};
    
public:
    void DistributeBroadcast(const std::string& data) {
        size_t group_index = round_robin_index.fetch_add(1) % client_groups.size();
        
        // Parallel broadcast to selected group
        std::thread([this, group_index, data]() {
            for (SOCKET client : client_groups[group_index]) {
                send_async(client, data);
            }
        }).detach();
    }
};

// Strategy 2: Differential updates (only send changes)
class DifferentialBroadcaster {
    std::unordered_map<SOCKET, std::string> last_sent_data;
    
public:
    void BroadcastChanges(const AeroflyBridgeData* current_data) {
        std::string current_json = BuildDataJSON(current_data);
        
        for (auto& [client, last_data] : last_sent_data) {
            if (current_json != last_data) {
                send(client, current_json.c_str(), current_json.length(), 0);
                last_data = current_json;
            }
        }
    }
};
```

---

## 🎓 Key Architectural Lessons

### 1. Single Responsibility Principle

**Each component has one clear job:**
- `SharedMemoryInterface` → Fast local data access
- `TCPServerInterface` → Network data distribution  
- `WebSocketServerInterface` → Browser connectivity
- `CommandProcessor` → Command parsing and routing
- `AeroflyBridge` → Component orchestration

### 2. Fail-Fast vs Fail-Safe Trade-offs

**Fail-Fast**: Shared memory initialization
```cpp
if (!shared_memory.Initialize()) {
    return false;  // Fail completely - core functionality broken
}
```

**Fail-Safe**: Network interfaces
```cpp
if (!tcp_server.Start()) {
    // Continue - other interfaces may still work
    OutputDebugStringA("TCP failed, but shared memory still available\n");
}
```

### 3. Performance vs Maintainability

**High-performance path** (Aerofly main thread):
```cpp
// Optimized for speed, less readable
inline void fast_update_path() {
    pData->data_valid = 0;
    *reinterpret_cast<uint64_t*>(&pData->timestamp_us) = get_time_us();
    process_messages_unrolled();  // Loop unrolling
    pData->data_valid = 1;
}
```

**Maintainable path** (Background threads):
```cpp
// Optimized for clarity and safety
void readable_processing_path() {
    try {
        auto messages = parse_incoming_commands();
        for (const auto& message : messages) {
            route_message_to_handler(message);
        }
    }
    catch (const std::exception& e) {
        log_error("Command processing failed", e.what());
    }
}
```

---

## 🚀 Next Steps

### Applying These Patterns

**Try implementing a simplified version:**

1. **Start with single interface** (TCP or Shared Memory)
2. **Add threading** for background operations
3. **Implement graceful degradation** 
4. **Add performance monitoring**
5. **Scale to multiple interfaces**

### Real-World Applications

**This architecture pattern applies to:**
- **Multi-protocol servers** (HTTP + WebSocket + TCP)
- **Real-time data distribution** systems
- **IoT device management** platforms
- **Game server** architectures
- **Financial trading** systems

---

**Understanding architecture is the key to building professional flight simulation tools.** The patterns shown here scale from simple DLLs to complex distributed systems.

<!-- Removed next-doc reference to avoid broken links in public repo. -->