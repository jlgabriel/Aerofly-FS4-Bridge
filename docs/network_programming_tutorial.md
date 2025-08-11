# Network Programming Patterns

**Building high-performance network servers for flight simulation**

Copyright (c) 2025 Juan Luis Gabriel

This tutorial explores the network programming techniques used in the Aerofly Bridge, teaching you how to build robust TCP and WebSocket servers from scratch. Learn modern networking patterns, protocol implementation, and how to handle hundreds of concurrent connections efficiently.

## 🎯 What You'll Learn

- **Socket programming** fundamentals (Winsock)
- **TCP server** implementation patterns
- **WebSocket protocol** from scratch (RFC 6455)
- **Non-blocking I/O** and select() patterns
- **Client connection management** at scale
- **Protocol design** for real-time data
- **Error handling** in network code
- **Performance optimization** techniques

## 📋 Prerequisites

- **[Thread-Safe Programming](03_thread_safety.md)** completed
- **C++ networking** (basic socket concepts)
- **HTTP protocol** awareness
- **JSON processing** familiarity

---

## 🌐 Network Architecture Overview

### The Bridge Network Stack

The Aerofly Bridge implements **dual network protocols** for different use cases:

```
                    ┌─────────────────┐
                    │ Aerofly Bridge  │
                    │ (Single Process)│
                    └─────────┬───────┘
                              │
                 ┌────────────┴────────────┐
                 ▼                         ▼
        ┌─────────────────┐       ┌─────────────────┐
        │ TCP Interface   │       │ WebSocket Intfc │
        │ (Port 12345/46) │       │ (Port 8765)     │
        └─────────┬───────┘       └─────────┬───────┘
                  │                         │
      ┌───────────┼───────────┐             │
      ▼           ▼           ▼             ▼
┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐
│Python    │ │C++ App   │ │Node.js   │ │Browser   │
│Scripts   │ │(Desktop) │ │Server    │ │App       │
└──────────┘ └──────────┘ └──────────┘ └──────────┘
```

### Protocol Comparison

| Feature | TCP Interface | WebSocket Interface |
|---------|---------------|-------------------|
| **Ports** | 12345 (data), 12346 (cmd) | 8765 (bidirectional) |
| **Protocol** | Raw TCP + JSON | HTTP Upgrade + WS Frames |
| **Handshake** | None | HTTP + SHA-1 + Base64 |
| **Framing** | Line-delimited JSON | RFC 6455 frames |
| **Bidirectional** | Separate ports | Single connection |
| **Browser Support** | No | Native |
| **Complexity** | Simple | Moderate |
| **Performance** | Highest | High |

---

## 🔌 TCP Server Implementation

### Basic TCP Server Foundation

**Core server structure** for handling multiple clients:

```cpp
class TCPServerInterface {
private:
    SOCKET server_socket;
    std::vector<SOCKET> client_sockets;
    std::thread server_thread;
    std::thread command_thread;
    std::atomic<bool> running{false};
    mutable std::mutex clients_mutex;
    bool winsock_initialized;
    
public:
    bool Start(uint16_t data_port, uint16_t command_port);
    void Stop();
    void BroadcastData(const AeroflyBridgeData* data);
    std::vector<std::string> GetPendingCommands();
    int GetClientCount() const;
    
private:
    void ServerLoop();
    void CommandLoop();
    void HandleNewConnection(SOCKET client);
    void RemoveClient(SOCKET client);
};
```

### Winsock Initialization and Cleanup

**Proper Windows socket initialization:**

```cpp
class WinsockManager {
private:
    bool initialized_;
    
public:
    WinsockManager() : initialized_(false) {
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            OutputDebugStringA(("WSAStartup failed: " + std::to_string(result) + "\n").c_str());
            return;
        }
        
        // Verify Winsock version
        if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
            OutputDebugStringA("Winsock 2.2 not supported\n");
            WSACleanup();
            return;
        }
        
        initialized_ = true;
        OutputDebugStringA("Winsock 2.2 initialized successfully\n");
    }
    
    ~WinsockManager() {
        if (initialized_) {
            WSACleanup();
            OutputDebugStringA("Winsock cleanup completed\n");
        }
    }
    
    bool IsInitialized() const { return initialized_; }
};

// Usage: RAII ensures proper cleanup
bool TCPServerInterface::Start(uint16_t data_port, uint16_t command_port) {
    static WinsockManager winsock;  // Initialize once
    
    if (!winsock.IsInitialized()) {
        OutputDebugStringA("ERROR: Winsock not initialized\n");
        return false;
    }
    
    // Continue with server setup...
    return CreateServerSocket(data_port);
}
```

### Server Socket Creation and Configuration

**Creating and configuring the listening socket:**

```cpp
bool TCPServerInterface::CreateServerSocket(uint16_t port) {
    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        OutputDebugStringA(("socket() failed: " + std::to_string(WSAGetLastError()) + "\n").c_str());
        return false;
    }
    
    // Enable address reuse (important for development)
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, 
                   reinterpret_cast<char*>(&opt), sizeof(opt)) == SOCKET_ERROR) {
        OutputDebugStringA("WARNING: SO_REUSEADDR failed\n");
    }
    
    // Bind to address
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Accept from any interface
    server_addr.sin_port = htons(port);
    
    if (bind(server_socket, reinterpret_cast<sockaddr*>(&server_addr), 
             sizeof(server_addr)) == SOCKET_ERROR) {
        int error = WSAGetLastError();
        OutputDebugStringA(("bind() failed: " + std::to_string(error) + "\n").c_str());
        closesocket(server_socket);
        server_socket = INVALID_SOCKET;
        return false;
    }
    
    // Start listening
    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        OutputDebugStringA(("listen() failed: " + std::to_string(WSAGetLastError()) + "\n").c_str());
        closesocket(server_socket);
        server_socket = INVALID_SOCKET;
        return false;
    }
    
    OutputDebugStringA(("TCP server listening on port " + std::to_string(port) + "\n").c_str());
    return true;
}
```

### Non-Blocking I/O with select()

**Handling multiple connections efficiently:**

```cpp
void TCPServerInterface::ServerLoop() {
    OutputDebugStringA("TCP server thread started\n");
    
    while (running) {
        // Use select() for non-blocking operation
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_socket, &read_fds);
        
        // Add all client sockets to the set
        std::vector<SOCKET> clients_snapshot;
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients_snapshot = client_sockets;
        }
        
        for (SOCKET client : clients_snapshot) {
            FD_SET(client, &read_fds);
        }
        
        // Set timeout for responsiveness
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 1000;  // 1ms timeout
        
        int activity = select(0, &read_fds, nullptr, nullptr, &timeout);
        
        if (activity == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error != WSAEINTR) {  // Ignore interrupt errors
                OutputDebugStringA(("select() error: " + std::to_string(error) + "\n").c_str());
            }
            continue;
        }
        
        if (activity == 0) {
            continue;  // Timeout - continue loop for responsiveness
        }
        
        // Check for new connection
        if (FD_ISSET(server_socket, &read_fds)) {
            HandleNewConnection();
        }
        
        // Check existing connections for data
        HandleClientData(clients_snapshot, read_fds);
    }
    
    OutputDebugStringA("TCP server thread stopped\n");
}

void TCPServerInterface::HandleNewConnection() {
    sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    SOCKET client_socket = accept(server_socket, 
                                 reinterpret_cast<sockaddr*>(&client_addr), 
                                 &addr_len);
    
    if (client_socket == INVALID_SOCKET) {
        int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {
            OutputDebugStringA(("accept() failed: " + std::to_string(error) + "\n").c_str());
        }
        return;
    }
    
    // Configure client socket
    ConfigureClientSocket(client_socket);
    
    // Add to client list
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        client_sockets.push_back(client_socket);
    }
    
    // Log connection
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    OutputDebugStringA(("New TCP client connected: " + std::string(client_ip) + 
                        ":" + std::to_string(ntohs(client_addr.sin_port)) + "\n").c_str());
}
```

### Client Socket Configuration

**Optimizing client connections for real-time data:**

```cpp
void TCPServerInterface::ConfigureClientSocket(SOCKET client_socket) {
    // Set non-blocking mode
    u_long mode = 1;  // Non-blocking
    if (ioctlsocket(client_socket, FIONBIO, &mode) == SOCKET_ERROR) {
        OutputDebugStringA("WARNING: Failed to set non-blocking mode\n");
    }
    
    // Disable Nagle algorithm for low latency
    int no_delay = 1;
    if (setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<char*>(&no_delay), sizeof(no_delay)) == SOCKET_ERROR) {
        OutputDebugStringA("WARNING: Failed to disable Nagle algorithm\n");
    }
    
    // Set send buffer size
    int send_buffer_size = 64 * 1024;  // 64KB
    if (setsockopt(client_socket, SOL_SOCKET, SO_SNDBUF,
                   reinterpret_cast<char*>(&send_buffer_size), sizeof(send_buffer_size)) == SOCKET_ERROR) {
        OutputDebugStringA("WARNING: Failed to set send buffer size\n");
    }
    
    // Set receive buffer size
    int recv_buffer_size = 16 * 1024;  // 16KB (commands are small)
    if (setsockopt(client_socket, SOL_SOCKET, SO_RCVBUF,
                   reinterpret_cast<char*>(&recv_buffer_size), sizeof(recv_buffer_size)) == SOCKET_ERROR) {
        OutputDebugStringA("WARNING: Failed to set receive buffer size\n");
    }
    
    // Set keepalive to detect dead connections
    int keepalive = 1;
    if (setsockopt(client_socket, SOL_SOCKET, SO_KEEPALIVE,
                   reinterpret_cast<char*>(&keepalive), sizeof(keepalive)) == SOCKET_ERROR) {
        OutputDebugStringA("WARNING: Failed to enable keepalive\n");
    }
    
    OutputDebugStringA("Client socket configured for optimal performance\n");
}
```

### Efficient Data Broadcasting

**Broadcasting JSON data to all connected clients:**

```cpp
void TCPServerInterface::BroadcastData(const AeroflyBridgeData* data) {
    if (!data || !running) return;
    
    // Throttling: respect AEROFLY_BRIDGE_BROADCAST_MS
    static uint64_t last_broadcast_us = 0;
    const uint64_t now_us = get_time_us();
    
    static uint32_t interval_us = []() {
        char buf[32] = {0};
        DWORD n = GetEnvironmentVariableA("AEROFLY_BRIDGE_BROADCAST_MS", buf, sizeof(buf));
        int ms = (n > 0) ? atoi(buf) : 20;  // Default 20ms = 50Hz
        if (ms < 5) ms = 5;  // Minimum 5ms
        return static_cast<uint32_t>(ms * 1000);  // Convert to microseconds
    }();
    
    if (now_us - last_broadcast_us < interval_us) {
        return;  // Skip this broadcast
    }
    last_broadcast_us = now_us;
    
    // Generate JSON once (expensive operation)
    const std::string json_data = BuildDataJSON(data);
    if (json_data.empty()) return;
    
    // Add newline delimiter for easier parsing
    const std::string message = json_data + "\n";
    
    // Get snapshot of clients (minimize lock time)
    std::vector<SOCKET> clients_snapshot;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients_snapshot = client_sockets;
    }
    
    if (clients_snapshot.empty()) return;
    
    // Broadcast to all clients (outside the lock)
    std::vector<SOCKET> clients_to_remove;
    
    for (SOCKET client : clients_snapshot) {
        int bytes_sent = send(client, message.c_str(), static_cast<int>(message.length()), 0);
        
        if (bytes_sent == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) {  // Real error, not just "would block"
                OutputDebugStringA(("Client send failed: " + std::to_string(error) + "\n").c_str());
                clients_to_remove.push_back(client);
            }
        }
        else if (bytes_sent != static_cast<int>(message.length())) {
            // Partial send - in a production system, you'd queue the remainder
            OutputDebugStringA("WARNING: Partial send to client\n");
        }
    }
    
    // Remove failed clients
    if (!clients_to_remove.empty()) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (SOCKET failed_client : clients_to_remove) {
            RemoveClientUnsafe(failed_client);
        }
    }
}

void TCPServerInterface::RemoveClientUnsafe(SOCKET client) {
    // Remove from client list (assumes lock is already held)
    auto it = std::find(client_sockets.begin(), client_sockets.end(), client);
    if (it != client_sockets.end()) {
        closesocket(*it);
        client_sockets.erase(it);
        OutputDebugStringA("Client disconnected and removed\n");
    }
}
```

---

## 🔗 WebSocket Server Implementation

### WebSocket Protocol Overview

**WebSocket handshake and frame structure:**

```
1. HTTP Upgrade Request:
   GET /websocket HTTP/1.1
   Upgrade: websocket
   Connection: Upgrade
   Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
   Sec-WebSocket-Version: 13

2. HTTP Upgrade Response:
   HTTP/1.1 101 Switching Protocols
   Upgrade: websocket
   Connection: Upgrade
   Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=

3. WebSocket Frames:
   ┌─────────────┬─────────────┬─────────────┬─────────────┐
   │ FIN │ OpCode │   Length    │   Payload   │
   │  1  │   4    │   7/16/64   │     Data    │
   └─────────────┴─────────────┴─────────────┴─────────────┘
```

### WebSocket Handshake Implementation

**HTTP upgrade to WebSocket protocol:**

```cpp
class WebSocketServerInterface {
private:
    // WebSocket GUID as per RFC 6455
    static constexpr const char* WS_MAGIC_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    
public:
    bool HandleWebSocketHandshake(SOCKET client, const std::string& request) {
        // Parse HTTP request
        auto headers = ParseHTTPHeaders(request);
        
        // Validate WebSocket headers
        if (headers["Upgrade"] != "websocket" || 
            headers["Connection"].find("Upgrade") == std::string::npos) {
            SendHTTPError(client, 400, "Bad Request");
            return false;
        }
        
        std::string ws_key = headers["Sec-WebSocket-Key"];
        if (ws_key.empty()) {
            SendHTTPError(client, 400, "Missing Sec-WebSocket-Key");
            return false;
        }
        
        // Generate accept key
        std::string accept_key = GenerateWebSocketAcceptKey(ws_key);
        
        // Send handshake response
        std::ostringstream response;
        response << "HTTP/1.1 101 Switching Protocols\r\n"
                 << "Upgrade: websocket\r\n"
                 << "Connection: Upgrade\r\n"
                 << "Sec-WebSocket-Accept: " << accept_key << "\r\n"
                 << "\r\n";
        
        std::string response_str = response.str();
        int bytes_sent = send(client, response_str.c_str(), 
                             static_cast<int>(response_str.length()), 0);
        
        if (bytes_sent != static_cast<int>(response_str.length())) {
            OutputDebugStringA("Failed to send WebSocket handshake response\n");
            return false;
        }
        
        OutputDebugStringA("WebSocket handshake completed successfully\n");
        return true;
    }

private:
    std::unordered_map<std::string, std::string> ParseHTTPHeaders(const std::string& request) {
        std::unordered_map<std::string, std::string> headers;
        std::istringstream stream(request);
        std::string line;
        
        // Skip request line
        std::getline(stream, line);
        
        // Parse headers
        while (std::getline(stream, line) && line != "\r") {
            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                std::string key = line.substr(0, colon_pos);
                std::string value = line.substr(colon_pos + 1);
                
                // Trim whitespace
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t\r\n") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);
                
                headers[key] = value;
            }
        }
        
        return headers;
    }
    
    std::string GenerateWebSocketAcceptKey(const std::string& client_key) {
        // Concatenate client key with magic GUID
        std::string combined = client_key + WS_MAGIC_GUID;
        
        // Calculate SHA-1 hash
        uint8_t sha1_hash[20];
        SHA1(reinterpret_cast<const uint8_t*>(combined.c_str()), 
             combined.length(), sha1_hash);
        
        // Encode as Base64
        return Base64Encode(sha1_hash, 20);
    }
};
```

### SHA-1 and Base64 Implementation

**WebSocket handshake cryptography (no external dependencies):**

```cpp
namespace WebSocketCrypto {
    // Minimal SHA-1 implementation for WebSocket handshake
    class SHA1 {
    private:
        uint32_t state[5];
        uint64_t count;
        uint8_t buffer[64];
        
        static uint32_t LeftRotate(uint32_t value, uint32_t bits) {
            return (value << bits) | (value >> (32 - bits));
        }
        
        void ProcessBlock(const uint8_t block[64]) {
            uint32_t w[80];
            uint32_t a, b, c, d, e;
            
            // Initialize array
            for (int i = 0; i < 16; i++) {
                w[i] = (block[i * 4] << 24) | (block[i * 4 + 1] << 16) |
                       (block[i * 4 + 2] << 8) | block[i * 4 + 3];
            }
            
            for (int i = 16; i < 80; i++) {
                w[i] = LeftRotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            }
            
            // Initialize hash value for this chunk
            a = state[0]; b = state[1]; c = state[2]; d = state[3]; e = state[4];
            
            // Main loop
            for (int i = 0; i < 80; i++) {
                uint32_t f, k;
                if (i < 20) {
                    f = (b & c) | ((~b) & d);
                    k = 0x5A827999;
                } else if (i < 40) {
                    f = b ^ c ^ d;
                    k = 0x6ED9EBA1;
                } else if (i < 60) {
                    f = (b & c) | (b & d) | (c & d);
                    k = 0x8F1BBCDC;
                } else {
                    f = b ^ c ^ d;
                    k = 0xCA62C1D6;
                }
                
                uint32_t temp = LeftRotate(a, 5) + f + e + k + w[i];
                e = d; d = c; c = LeftRotate(b, 30); b = a; a = temp;
            }
            
            // Add this chunk's hash to result so far
            state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
        }
        
    public:
        SHA1() {
            state[0] = 0x67452301;
            state[1] = 0xEFCDAB89;
            state[2] = 0x98BADCFE;
            state[3] = 0x10325476;
            state[4] = 0xC3D2E1F0;
            count = 0;
        }
        
        void Update(const uint8_t* data, size_t length) {
            for (size_t i = 0; i < length; i++) {
                buffer[count % 64] = data[i];
                count++;
                
                if (count % 64 == 0) {
                    ProcessBlock(buffer);
                }
            }
        }
        
        void Final(uint8_t hash[20]) {
            // Padding
            uint64_t bit_count = count * 8;
            uint8_t padding = 0x80;
            Update(&padding, 1);
            
            while (count % 64 != 56) {
                padding = 0x00;
                Update(&padding, 1);
            }
            
            // Append length
            for (int i = 7; i >= 0; i--) {
                padding = static_cast<uint8_t>((bit_count >> (i * 8)) & 0xFF);
                Update(&padding, 1);
            }
            
            // Extract hash
            for (int i = 0; i < 5; i++) {
                hash[i * 4] = static_cast<uint8_t>((state[i] >> 24) & 0xFF);
                hash[i * 4 + 1] = static_cast<uint8_t>((state[i] >> 16) & 0xFF);
                hash[i * 4 + 2] = static_cast<uint8_t>((state[i] >> 8) & 0xFF);
                hash[i * 4 + 3] = static_cast<uint8_t>(state[i] & 0xFF);
            }
        }
    };
    
    std::string Base64Encode(const uint8_t* data, size_t length) {
        static const char* base64_chars = 
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        
        std::string result;
        result.reserve(((length + 2) / 3) * 4);
        
        for (size_t i = 0; i < length; i += 3) {
            uint32_t triple = (data[i] << 16) | 
                             ((i + 1 < length ? data[i + 1] : 0) << 8) |
                             (i + 2 < length ? data[i + 2] : 0);
            
            result.push_back(base64_chars[(triple >> 18) & 0x3F]);
            result.push_back(base64_chars[(triple >> 12) & 0x3F]);
            result.push_back(i + 1 < length ? base64_chars[(triple >> 6) & 0x3F] : '=');
            result.push_back(i + 2 < length ? base64_chars[triple & 0x3F] : '=');
        }
        
        return result;
    }
    
    void ComputeSHA1(const uint8_t* data, size_t length, uint8_t hash[20]) {
        SHA1 sha1;
        sha1.Update(data, length);
        sha1.Final(hash);
    }
}

std::string WebSocketServerInterface::GenerateWebSocketAcceptKey(const std::string& client_key) {
    std::string combined = client_key + WS_MAGIC_GUID;
    uint8_t sha1_hash[20];
    
    WebSocketCrypto::ComputeSHA1(
        reinterpret_cast<const uint8_t*>(combined.c_str()),
        combined.length(),
        sha1_hash
    );
    
    return WebSocketCrypto::Base64Encode(sha1_hash, 20);
}
```

### WebSocket Frame Processing

**Parsing and creating WebSocket frames:**

```cpp
struct WebSocketFrame {
    bool fin;
    uint8_t opcode;
    bool masked;
    uint64_t payload_length;
    uint8_t mask[4];
    std::vector<uint8_t> payload;
    
    enum OpCode {
        CONTINUATION = 0x0,
        TEXT = 0x1,
        BINARY = 0x2,
        CLOSE = 0x8,
        PING = 0x9,
        PONG = 0xA
    };
};

class WebSocketFrameProcessor {
public:
    static std::optional<WebSocketFrame> ParseFrame(const std::vector<uint8_t>& data, size_t& bytes_consumed) {
        if (data.size() < 2) {
            return std::nullopt;  // Need at least 2 bytes
        }
        
        WebSocketFrame frame;
        size_t offset = 0;
        
        // First byte: FIN + OpCode
        uint8_t first_byte = data[offset++];
        frame.fin = (first_byte & 0x80) != 0;
        frame.opcode = first_byte & 0x0F;
        
        // Second byte: MASK + Payload length
        uint8_t second_byte = data[offset++];
        frame.masked = (second_byte & 0x80) != 0;
        uint8_t length_indicator = second_byte & 0x7F;
        
        // Determine payload length
        if (length_indicator < 126) {
            frame.payload_length = length_indicator;
        } else if (length_indicator == 126) {
            if (data.size() < offset + 2) return std::nullopt;
            frame.payload_length = (data[offset] << 8) | data[offset + 1];
            offset += 2;
        } else {  // length_indicator == 127
            if (data.size() < offset + 8) return std::nullopt;
            frame.payload_length = 0;
            for (int i = 0; i < 8; i++) {
                frame.payload_length = (frame.payload_length << 8) | data[offset + i];
            }
            offset += 8;
        }
        
        // Read mask (if present)
        if (frame.masked) {
            if (data.size() < offset + 4) return std::nullopt;
            memcpy(frame.mask, &data[offset], 4);
            offset += 4;
        }
        
        // Read payload
        if (data.size() < offset + frame.payload_length) {
            return std::nullopt;  // Incomplete frame
        }
        
        frame.payload.resize(frame.payload_length);
        for (uint64_t i = 0; i < frame.payload_length; i++) {
            uint8_t byte = data[offset + i];
            if (frame.masked) {
                byte ^= frame.mask[i % 4];  // Unmask
            }
            frame.payload[i] = byte;
        }
        
        bytes_consumed = offset + frame.payload_length;
        return frame;
    }
    
    static std::vector<uint8_t> CreateFrame(const std::string& payload, uint8_t opcode = WebSocketFrame::TEXT) {
        std::vector<uint8_t> frame;
        
        // First byte: FIN=1, OpCode
        frame.push_back(0x80 | opcode);
        
        // Payload length
        size_t payload_size = payload.size();
        if (payload_size < 126) {
            frame.push_back(static_cast<uint8_t>(payload_size));
        } else if (payload_size <= 0xFFFF) {
            frame.push_back(126);
            frame.push_back(static_cast<uint8_t>((payload_size >> 8) & 0xFF));
            frame.push_back(static_cast<uint8_t>(payload_size & 0xFF));
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; i--) {
                frame.push_back(static_cast<uint8_t>((payload_size >> (i * 8)) & 0xFF));
            }
        }
        
        // Payload (server frames are not masked)
        frame.insert(frame.end(), payload.begin(), payload.end());
        
        return frame;
    }
};
```

### WebSocket Server Loop

**Handling WebSocket connections with frame processing:**

```cpp
void WebSocketServerInterface::ServerLoop() {
    OutputDebugStringA("WebSocket server thread started\n");
    
    while (running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_socket, &read_fds);
        
        // Add client sockets
        std::vector<SOCKET> clients_snapshot;
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients_snapshot = client_sockets;
            for (SOCKET client : clients_snapshot) {
                FD_SET(client, &read_fds);
            }
        }
        
        struct timeval timeout = {0, 1000};  // 1ms timeout
        int activity = select(0, &read_fds, nullptr, nullptr, &timeout);
        
        if (activity > 0) {
            // Check for new HTTP/WebSocket connection
            if (FD_ISSET(server_socket, &read_fds)) {
                HandleNewWebSocketConnection();
            }
            
            // Check existing WebSocket connections
            HandleWebSocketFrames(clients_snapshot, read_fds);
        }
    }
    
    OutputDebugStringA("WebSocket server thread stopped\n");
}

void WebSocketServerInterface::HandleNewWebSocketConnection() {
    SOCKET client = accept(server_socket, nullptr, nullptr);
    if (client == INVALID_SOCKET) return;
    
    // Read HTTP upgrade request
    char buffer[4096];
    int bytes_received = recv(client, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        std::string request(buffer);
        
        if (HandleWebSocketHandshake(client, request)) {
            // Configure for WebSocket communication
            ConfigureClientSocket(client);
            
            // Add to WebSocket clients
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                client_sockets.push_back(client);
                recv_buffers[client] = std::string();  // Initialize buffer
            }
            
            OutputDebugStringA("WebSocket client connected successfully\n");
        } else {
            closesocket(client);
            OutputDebugStringA("WebSocket handshake failed\n");
        }
    } else {
        closesocket(client);
    }
}

void WebSocketServerInterface::HandleWebSocketFrames(
    const std::vector<SOCKET>& clients, const fd_set& read_fds) {
    
    std::vector<SOCKET> clients_to_remove;
    
    for (SOCKET client : clients) {
        if (!FD_ISSET(client, &read_fds)) continue;
        
        char buffer[4096];
        int bytes_received = recv(client, buffer, sizeof(buffer), 0);
        
        if (bytes_received > 0) {
            // Add to client's receive buffer
            std::string& client_buffer = recv_buffers[client];
            client_buffer.append(buffer, bytes_received);
            
            // Process complete frames
            ProcessWebSocketFrames(client, client_buffer);
            
        } else if (bytes_received == 0) {
            // Client disconnected gracefully
            clients_to_remove.push_back(client);
        } else {
            // Socket error
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) {
                clients_to_remove.push_back(client);
            }
        }
    }
    
    // Remove disconnected clients
    if (!clients_to_remove.empty()) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (SOCKET client : clients_to_remove) {
            RemoveWebSocketClient(client);
        }
    }
}

void WebSocketServerInterface::ProcessWebSocketFrames(SOCKET client, std::string& buffer) {
    std::vector<uint8_t> data(buffer.begin(), buffer.end());
    size_t offset = 0;
    
    while (offset < data.size()) {
        size_t bytes_consumed = 0;
        auto frame = WebSocketFrameProcessor::ParseFrame(
            std::vector<uint8_t>(data.begin() + offset, data.end()), 
            bytes_consumed
        );
        
        if (!frame) {
            break;  // Incomplete frame, wait for more data
        }
        
        // Process the frame
        HandleWebSocketFrame(client, *frame);
        
        offset += bytes_consumed;
    }
    
    // Remove processed data from buffer
    if (offset > 0) {
        buffer.erase(0, offset);
    }
}

void WebSocketServerInterface::HandleWebSocketFrame(SOCKET client, const WebSocketFrame& frame) {
    switch (frame.opcode) {
        case WebSocketFrame::TEXT: {
            // JSON command received
            std::string command(frame.payload.begin(), frame.payload.end());
            
            // Queue command for processing
            {
                std::lock_guard<std::mutex> lock(command_mutex);
                command_queue.push(command);
            }
            
            OutputDebugStringA(("WebSocket command received: " + command + "\n").c_str());
            break;
        }
        
        case WebSocketFrame::PING: {
            // Respond with PONG
            std::string pong_payload(frame.payload.begin(), frame.payload.end());
            SendWebSocketPong(client, pong_payload);
            break;
        }
        
        case WebSocketFrame::CLOSE: {
            // Client initiated close
            SendWebSocketClose(client);
            break;
        }
        
        default:
            OutputDebugStringA(("Unsupported WebSocket opcode: " + 
                               std::to_string(frame.opcode) + "\n").c_str());
            break;
    }
}
```

---

## ⚡ Performance Optimization Techniques

### Connection Pooling and Management

**Efficient client connection handling:**

```cpp
class ConnectionPool {
private:
    struct Connection {
        SOCKET socket;
        uint64_t last_activity_us;
        uint32_t bytes_sent;
        uint32_t bytes_received;
        bool is_websocket;
        std::string receive_buffer;
    };
    
    std::unordered_map<SOCKET, Connection> connections_;
    mutable std::shared_mutex connections_mutex_;
    std::atomic<size_t> active_connections_{0};
    
public:
    void AddConnection(SOCKET socket, bool is_websocket) {
        Connection conn{};
        conn.socket = socket;
        conn.last_activity_us = get_time_us();
        conn.bytes_sent = 0;
        conn.bytes_received = 0;
        conn.is_websocket = is_websocket;
        
        {
            std::unique_lock<std::shared_mutex> lock(connections_mutex_);
            connections_[socket] = std::move(conn);
        }
        
        active_connections_.fetch_add(1, std::memory_order_relaxed);
        OutputDebugStringA(("Connection added, total: " + 
                           std::to_string(active_connections_.load()) + "\n").c_str());
    }
    
    void RemoveConnection(SOCKET socket) {
        {
            std::unique_lock<std::shared_mutex> lock(connections_mutex_);
            auto it = connections_.find(socket);
            if (it != connections_.end()) {
                closesocket(it->second.socket);
                connections_.erase(it);
            }
        }
        
        active_connections_.fetch_sub(1, std::memory_order_relaxed);
    }
    
    void UpdateActivity(SOCKET socket, uint32_t bytes_transferred, bool is_send) {
        std::shared_lock<std::shared_mutex> lock(connections_mutex_);
        auto it = connections_.find(socket);
        if (it != connections_.end()) {
            it->second.last_activity_us = get_time_us();
            if (is_send) {
                it->second.bytes_sent += bytes_transferred;
            } else {
                it->second.bytes_received += bytes_transferred;
            }
        }
    }
    
    std::vector<SOCKET> GetIdleConnections(uint64_t timeout_us) const {
        std::vector<SOCKET> idle_sockets;
        const uint64_t now_us = get_time_us();
        
        std::shared_lock<std::shared_mutex> lock(connections_mutex_);
        for (const auto& [socket, conn] : connections_) {
            if (now_us - conn.last_activity_us > timeout_us) {
                idle_sockets.push_back(socket);
            }
        }
        
        return idle_sockets;
    }
    
    size_t GetConnectionCount() const {
        return active_connections_.load(std::memory_order_relaxed);
    }
    
    // Statistics for monitoring
    struct Stats {
        size_t total_connections;
        size_t websocket_connections;
        size_t tcp_connections;
        uint64_t total_bytes_sent;
        uint64_t total_bytes_received;
    };
    
    Stats GetStats() const {
        Stats stats{};
        
        std::shared_lock<std::shared_mutex> lock(connections_mutex_);
        stats.total_connections = connections_.size();
        
        for (const auto& [socket, conn] : connections_) {
            if (conn.is_websocket) {
                stats.websocket_connections++;
            } else {
                stats.tcp_connections++;
            }
            stats.total_bytes_sent += conn.bytes_sent;
            stats.total_bytes_received += conn.bytes_received;
        }
        
        return stats;
    }
};
```

### Optimized JSON Broadcasting

**Zero-copy broadcasting with shared JSON:**

```cpp
class OptimizedBroadcaster {
private:
    struct BroadcastPacket {
        std::string tcp_data;      // JSON + newline
        std::vector<uint8_t> ws_frame;  // WebSocket frame
        uint64_t timestamp_us;
        uint32_t sequence_number;
    };
    
    std::shared_ptr<BroadcastPacket> current_packet_;
    mutable std::shared_mutex packet_mutex_;
    std::atomic<uint32_t> sequence_counter_{0};
    
public:
    void PrepareNewBroadcast(const AeroflyBridgeData* data) {
        // Generate JSON once
        std::string json = BuildDataJSON(data);
        if (json.empty()) return;
        
        // Create new packet
        auto packet = std::make_shared<BroadcastPacket>();
        packet->tcp_data = json + "\n";  // TCP: JSON + delimiter
        packet->ws_frame = WebSocketFrameProcessor::CreateFrame(json);  // WebSocket: framed JSON
        packet->timestamp_us = get_time_us();
        packet->sequence_number = sequence_counter_.fetch_add(1, std::memory_order_relaxed);
        
        // Atomically update current packet
        {
            std::unique_lock<std::shared_mutex> lock(packet_mutex_);
            current_packet_ = packet;
        }
    }
    
    bool BroadcastToTCPClients(const std::vector<SOCKET>& clients) {
        std::shared_ptr<BroadcastPacket> packet;
        {
            std::shared_lock<std::shared_mutex> lock(packet_mutex_);
            packet = current_packet_;
        }
        
        if (!packet) return false;
        
        // Broadcast to all TCP clients (no copying)
        for (SOCKET client : clients) {
            int bytes_sent = send(client, packet->tcp_data.c_str(), 
                                 static_cast<int>(packet->tcp_data.length()), 0);
            
            if (bytes_sent == SOCKET_ERROR) {
                // Handle error (client will be removed by caller)
                continue;
            }
        }
        
        return true;
    }
    
    bool BroadcastToWebSocketClients(const std::vector<SOCKET>& clients) {
        std::shared_ptr<BroadcastPacket> packet;
        {
            std::shared_lock<std::shared_mutex> lock(packet_mutex_);
            packet = current_packet_;
        }
        
        if (!packet) return false;
        
        // Broadcast to all WebSocket clients (no copying)
        for (SOCKET client : clients) {
            int bytes_sent = send(client, 
                                 reinterpret_cast<const char*>(packet->ws_frame.data()),
                                 static_cast<int>(packet->ws_frame.size()), 0);
            
            if (bytes_sent == SOCKET_ERROR) {
                // Handle error (client will be removed by caller)
                continue;
            }
        }
        
        return true;
    }
};
```

---

## 🚨 Error Handling and Recovery

### Network Error Classification

**Different types of network errors require different handling:**

```cpp
enum class NetworkErrorType {
    TEMPORARY,      // WSAEWOULDBLOCK, WSAEINTR - retry later
    CONNECTION,     // WSAECONNRESET, WSAECONNABORTED - remove client
    RESOURCE,       // WSAENOBUFS, WSAEMFILE - backoff and retry
    FATAL          // WSAEINVAL, WSAENOTSOCK - shutdown component
};

class NetworkErrorHandler {
public:
    static NetworkErrorType ClassifyError(int winsock_error) {
        switch (winsock_error) {
            case WSAEWOULDBLOCK:
            case WSAEINTR:
            case WSAEINPROGRESS:
                return NetworkErrorType::TEMPORARY;
                
            case WSAECONNRESET:
            case WSAECONNABORTED:
            case WSAECONNREFUSED:
            case WSAENOTCONN:
            case WSAESHUTDOWN:
                return NetworkErrorType::CONNECTION;
                
            case WSAENOBUFS:
            case WSAEMFILE:
            case WSAETOOMANYREFS:
                return NetworkErrorType::RESOURCE;
                
            case WSAEINVAL:
            case WSAENOTSOCK:
            case WSAEFAULT:
            default:
                return NetworkErrorType::FATAL;
        }
    }
    
    static std::string GetErrorDescription(int winsock_error) {
        char* error_msg = nullptr;
        FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
            nullptr,
            winsock_error,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<char*>(&error_msg),
            0,
            nullptr
        );
        
        std::string description = error_msg ? error_msg : "Unknown error";
        if (error_msg) LocalFree(error_msg);
        
        return description + " (Code: " + std::to_string(winsock_error) + ")";
    }
    
    static bool ShouldRetry(NetworkErrorType error_type, int retry_count, int max_retries) {
        switch (error_type) {
            case NetworkErrorType::TEMPORARY:
                return retry_count < max_retries;
            case NetworkErrorType::RESOURCE:
                return retry_count < (max_retries / 2);  // Fewer retries for resource issues
            default:
                return false;
        }
    }
    
    static std::chrono::milliseconds GetRetryDelay(NetworkErrorType error_type, int retry_count) {
        switch (error_type) {
            case NetworkErrorType::TEMPORARY:
                return std::chrono::milliseconds(1 << std::min(retry_count, 6));  // Exponential backoff, max 64ms
            case NetworkErrorType::RESOURCE:
                return std::chrono::milliseconds(100 * (retry_count + 1));  // Linear backoff
            default:
                return std::chrono::milliseconds(0);
        }
    }
};

// Usage in send operations
bool SafeSend(SOCKET socket, const char* data, int length) {
    int retry_count = 0;
    const int max_retries = 3;
    
    while (retry_count <= max_retries) {
        int bytes_sent = send(socket, data, length, 0);
        
        if (bytes_sent == length) {
            return true;  // Success
        }
        
        if (bytes_sent == SOCKET_ERROR) {
            int error = WSAGetLastError();
            NetworkErrorType error_type = NetworkErrorHandler::ClassifyError(error);
            
            if (NetworkErrorHandler::ShouldRetry(error_type, retry_count, max_retries)) {
                auto delay = NetworkErrorHandler::GetRetryDelay(error_type, retry_count);
                std::this_thread::sleep_for(delay);
                retry_count++;
                continue;
            } else {
                OutputDebugStringA(("Send failed: " + 
                    NetworkErrorHandler::GetErrorDescription(error) + "\n").c_str());
                return false;
            }
        } else {
            // Partial send - in production, you'd handle this properly
            OutputDebugStringA("WARNING: Partial send occurred\n");
            return false;
        }
    }
    
    return false;
}
```

---

## 🎓 Protocol Design Best Practices

### 1. Message Framing

**Clear message boundaries prevent parsing issues:**

```cpp
// ✅ Good: Length-prefixed messages
struct MessageHeader {
    uint32_t magic_number;   // 0xDEADBEEF
    uint32_t message_length; // Payload size
    uint32_t message_type;   // Command, data, etc.
    uint32_t checksum;       // Simple validation
};

// ✅ Good: Delimited messages (simpler)
"{ \"variable\": \"Aircraft.Altitude\", \"value\": 1000.0 }\n"

// ❌ Bad: No framing (parsing nightmare)
"{ \"variable\": \"Aircraft.Altitude\", \"value\": 1000.0 }{ \"variable\": \"Aircraft.Airspeed\""
```

### 2. Version Negotiation

**Handle protocol evolution gracefully:**

```cpp
struct ProtocolVersion {
    uint16_t major;
    uint16_t minor;
    
    bool IsCompatible(const ProtocolVersion& other) const {
        return major == other.major && minor >= other.minor;
    }
};

// Client handshake includes version
"{ \"type\": \"handshake\", \"version\": { \"major\": 1, \"minor\": 2 } }"

// Server responds with supported version
"{ \"type\": \"handshake_response\", \"version\": { \"major\": 1, \"minor\": 1 }, \"status\": \"ok\" }"
```

### 3. Heartbeat and Keepalive

**Detect dead connections reliably:**

```cpp
class ConnectionHeartbeat {
private:
    std::unordered_map<SOCKET, uint64_t> last_ping_times_;
    std::atomic<uint64_t> ping_interval_us_{30 * 1000 * 1000};  // 30 seconds
    
public:
    void SendPings(const std::vector<SOCKET>& clients) {
        const uint64_t now_us = get_time_us();
        const std::string ping_message = "{ \"type\": \"ping\", \"timestamp\": " + 
                                        std::to_string(now_us) + " }\n";
        
        for (SOCKET client : clients) {
            auto it = last_ping_times_.find(client);
            if (it == last_ping_times_.end() || 
                now_us - it->second >= ping_interval_us_.load()) {
                
                if (SafeSend(client, ping_message.c_str(), ping_message.length())) {
                    last_ping_times_[client] = now_us;
                }
            }
        }
    }
    
    std::vector<SOCKET> GetDeadConnections(const std::vector<SOCKET>& clients) {
        const uint64_t now_us = get_time_us();
        const uint64_t timeout_us = ping_interval_us_.load() * 3;  // 3x ping interval
        
        std::vector<SOCKET> dead_connections;
        
        for (SOCKET client : clients) {
            auto it = last_ping_times_.find(client);
            if (it != last_ping_times_.end() && 
                now_us - it->second > timeout_us) {
                dead_connections.push_back(client);
            }
        }
        
        return dead_connections;
    }
};
```

---

<!-- Removed "Next Steps" and cross-document references to avoid broken links in public repo. -->
- **Streaming services** (adaptive bitrate)
- **Financial data feeds** (ultra-low latency)

---

**Network programming is the backbone of modern distributed applications.** Master these patterns and you'll be able to build robust, scalable communication systems for any domain.

**Next**: [Performance Optimization](05_performance_guide.md) - Learn to measure, analyze, and optimize every aspect of your flight simulation systems.