///////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Aerofly FS4 Reader DLL - Simplified Read-Only Version
 *
 * A streamlined version of the Aerofly Bridge focused exclusively on:
 * - Reading simulation data from Aerofly FS4
 * - Exposing data via Shared Memory for local applications
 * - Streaming JSON data via TCP for network clients
 *
 * This version DOES NOT support:
 * - Sending commands back to the simulator
 * - WebSocket connections (removed for simplicity)
 * - Two-way control of aircraft systems
 *
 * Perfect for:
 * - Flight data logging applications
 * - Map/position trackers
 * - Telemetry displays
 * - Integration with external monitoring tools
 *
 * Architecture:
 *   Aerofly FS4 (50-60 Hz) --[Messages]--> AeroflyReader DLL
 *                                              |
 *                                              +---> Shared Memory (local apps)
 *                                              +---> TCP Port 12345 (JSON streaming)
 */
///////////////////////////////////////////////////////////////////////////////////////////////////

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <string>
#include <cmath>
#include <memory>
#include <atomic>
#include <functional>

#pragma comment(lib, "ws2_32.lib")

// Minimal debug logging macro (disabled in release builds)
#if defined(NDEBUG)
#define DBG(msg) do {} while(0)
#else
#define DBG(msg) OutputDebugStringA(msg)
#endif

// DLL Version
#define AEROFLY_READER_VERSION "1.1.0"

// Sanitize double values (NaN/Inf become 0.0 for valid JSON)
static inline double safe_double(double v) {
    return (std::isfinite(v)) ? v : 0.0;
}

// Handler registration macros (reduce repetitive code, with NaN/Inf protection)
#define HANDLE_DOUBLE(msgName, field) \
    message_handlers[msgName.GetID()] = [this](const auto& msg) { pData->field = safe_double(msg.GetDouble()); }
#define HANDLE_VECTOR3D(msgName, field) \
    message_handlers[msgName.GetID()] = [this](const auto& msg) { pData->field = msg.GetVector3d(); }
#define HANDLE_VECTOR2D(msgName, field) \
    message_handlers[msgName.GetID()] = [this](const auto& msg) { pData->field = msg.GetVector2d(); }

// High-resolution timestamp in microseconds
static inline uint64_t get_time_us() {
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<uint64_t>((now.QuadPart * 1000000ULL) / frequency.QuadPart);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// CONFIGURATION
///////////////////////////////////////////////////////////////////////////////////////////////////
/*
Environment Variables (optional):
  - AEROFLY_READER_BROADCAST_MS : Telemetry broadcast interval in ms (default: 20 = 50Hz)
  - AEROFLY_READER_TCP_PORT     : TCP data port (default: 12345)

Threading Model:
  - Aerofly main thread calls Update() at 50-60 Hz
  - TCPDataServer spawns one thread to accept client connections
  - SharedMemory is updated directly from the Aerofly thread

Data Flow (READ-ONLY):
  Aerofly SDK --> Update(received_messages) --> SharedMemory --> TCP Broadcast --> Clients
*/

// Include Aerofly SDK header (must be downloaded separately)
#include "tm_external_message.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
// SIMPLIFIED DATA STRUCTURE - Core Variables Only
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Simplified data structure with essential flight variables
 *
 * Contains the most commonly needed variables for integrations:
 * - Position and orientation
 * - Speeds and altitudes
 * - Navigation data
 * - Basic aircraft state
 */
struct AeroflyReaderData {
    // ========================================================================
    // HEADER (16 bytes)
    // ========================================================================
    uint64_t timestamp_us;              // Microseconds since start
    uint32_t data_valid;                // 1 = valid, 0 = invalid
    uint32_t update_counter;            // Increments each update

    // ========================================================================
    // AIRCRAFT POSITION & ORIENTATION
    // ========================================================================
    double latitude;                    // Aircraft.Latitude (degrees)
    double longitude;                   // Aircraft.Longitude (degrees)
    double altitude;                    // Aircraft.Altitude (feet MSL)
    double height;                      // Aircraft.Height (feet AGL)
    double pitch;                       // Aircraft.Pitch (radians)
    double bank;                        // Aircraft.Bank (radians)
    double true_heading;                // Aircraft.TrueHeading (radians)
    double magnetic_heading;            // Aircraft.MagneticHeading (radians)

    // ========================================================================
    // SPEEDS
    // ========================================================================
    double indicated_airspeed;          // Aircraft.IndicatedAirspeed (m/s)
    double ground_speed;                // Aircraft.GroundSpeed (m/s)
    double vertical_speed;              // Aircraft.VerticalSpeed (m/s)
    double mach_number;                 // Aircraft.MachNumber
    double angle_of_attack;             // Aircraft.AngleOfAttack (radians)

    // ========================================================================
    // PHYSICS VECTORS
    // ========================================================================
    tm_vector3d position;               // Aircraft.Position (world coordinates)
    tm_vector3d velocity;               // Aircraft.Velocity (m/s)
    tm_vector3d acceleration;           // Aircraft.Acceleration (m/s^2)
    tm_vector3d wind;                   // Aircraft.Wind (m/s)

    // ========================================================================
    // AIRCRAFT STATE
    // ========================================================================
    double on_ground;                   // Aircraft.OnGround (0/1)
    double gear;                        // Aircraft.Gear (0-1)
    double flaps;                       // Aircraft.Flaps (0-1)
    double throttle;                    // Aircraft.Throttle (0-1)
    double parking_brake;               // Aircraft.ParkingBrake (0/1)

    // ========================================================================
    // ENGINE STATE
    // ========================================================================
    double engine_running_1;            // Aircraft.EngineRunning1 (0/1)
    double engine_running_2;            // Aircraft.EngineRunning2 (0/1)
    double engine_throttle_1;           // Aircraft.EngineThrottle1 (0-1)
    double engine_throttle_2;           // Aircraft.EngineThrottle2 (0-1)

    // ========================================================================
    // NAVIGATION
    // ========================================================================
    double nav1_frequency;              // Navigation.NAV1Frequency (MHz)
    double nav2_frequency;              // Navigation.NAV2Frequency (MHz)
    double com1_frequency;              // Communication.COM1Frequency (MHz)
    double com2_frequency;              // Communication.COM2Frequency (MHz)
    double selected_course_1;           // Navigation.SelectedCourse1 (radians)
    double selected_course_2;           // Navigation.SelectedCourse2 (radians)

    // ========================================================================
    // AUTOPILOT (read-only state)
    // ========================================================================
    double autopilot_master;            // Autopilot.Master (0/1)
    double autopilot_heading;           // Autopilot.Heading (radians)
    double autopilot_altitude;          // Autopilot.SelectedAltitude (feet)
    double autopilot_vertical_speed;    // Autopilot.SelectedVerticalSpeed (ft/min)
    double autopilot_speed;             // Autopilot.SelectedSpeed (knots)

    // ========================================================================
    // PERFORMANCE SPEEDS (V-speeds)
    // ========================================================================
    double vs0;                         // Performance.Speed.VS0 (stall flaps down)
    double vs1;                         // Performance.Speed.VS1 (stall clean)
    double vfe;                         // Performance.Speed.VFE (max flap extended)
    double vno;                         // Performance.Speed.VNO (max structural cruise)
    double vne;                         // Performance.Speed.VNE (never exceed)

    // ========================================================================
    // NEAREST AIRPORT
    // ========================================================================
    double nearest_airport_elevation;   // Aircraft.NearestAirportElevation
    tm_vector2d nearest_airport_location; // Aircraft.NearestAirportLocation

    // ========================================================================
    // STRING VARIABLES
    // ========================================================================
    char aircraft_name[32];             // Aircraft.Name
    char nearest_airport_id[8];         // Aircraft.NearestAirportIdentifier
    char nearest_airport_name[64];      // Aircraft.NearestAirportName
};

// Forward declaration for JSON builder
static std::string BuildDataJSON(const AeroflyReaderData* data);

///////////////////////////////////////////////////////////////////////////////////////////////////
// SHARED MEMORY INTERFACE (Read-Only)
///////////////////////////////////////////////////////////////////////////////////////////////////

class SharedMemoryReader {
private:
    HANDLE hMapFile;
    AeroflyReaderData* pData;
    std::mutex data_mutex;
    bool initialized;

    // Hash map for O(1) message handler lookup
    using MessageHandler = std::function<void(const tm_external_message&)>;
    std::unordered_map<uint64_t, MessageHandler> message_handlers;

    void ProcessStringMessage(const tm_external_message& message, char* destination,
                             size_t dest_size, const char* default_value) {
        if (message.GetDataType() == tm_msg_data_type::String ||
            message.GetDataType() == tm_msg_data_type::String8) {
            try {
                const tm_string tm_value = message.GetString();
                const std::string value = tm_value.c_str();

                if (!value.empty() && value.length() < dest_size) {
                    strncpy_s(destination, dest_size, value.c_str(), _TRUNCATE);
                } else {
                    strncpy_s(destination, dest_size, default_value, _TRUNCATE);
                }
            } catch (...) {
                strncpy_s(destination, dest_size, default_value, _TRUNCATE);
            }
        } else {
            strncpy_s(destination, dest_size, default_value, _TRUNCATE);
        }
        if (dest_size > 0) destination[dest_size - 1] = '\0';
    }

public:
    SharedMemoryReader() : hMapFile(NULL), pData(nullptr), initialized(false) {
        InitializeHandlers();
    }

    ~SharedMemoryReader() {
        Cleanup();
    }

    void InitializeHandlers() {
        // Position & Orientation
        HANDLE_DOUBLE(MessageAircraftLatitude, latitude);
        HANDLE_DOUBLE(MessageAircraftLongitude, longitude);
        HANDLE_DOUBLE(MessageAircraftAltitude, altitude);
        HANDLE_DOUBLE(MessageAircraftHeight, height);
        HANDLE_DOUBLE(MessageAircraftPitch, pitch);
        HANDLE_DOUBLE(MessageAircraftBank, bank);
        HANDLE_DOUBLE(MessageAircraftTrueHeading, true_heading);
        HANDLE_DOUBLE(MessageAircraftMagneticHeading, magnetic_heading);

        // Speeds
        HANDLE_DOUBLE(MessageAircraftIndicatedAirspeed, indicated_airspeed);
        HANDLE_DOUBLE(MessageAircraftGroundSpeed, ground_speed);
        HANDLE_DOUBLE(MessageAircraftVerticalSpeed, vertical_speed);
        HANDLE_DOUBLE(MessageAircraftMachNumber, mach_number);
        HANDLE_DOUBLE(MessageAircraftAngleOfAttack, angle_of_attack);

        // Physics Vectors
        HANDLE_VECTOR3D(MessageAircraftPosition, position);
        HANDLE_VECTOR3D(MessageAircraftVelocity, velocity);
        HANDLE_VECTOR3D(MessageAircraftAcceleration, acceleration);
        HANDLE_VECTOR3D(MessageAircraftWind, wind);

        // Aircraft State
        HANDLE_DOUBLE(MessageAircraftOnGround, on_ground);
        HANDLE_DOUBLE(MessageAircraftGear, gear);
        HANDLE_DOUBLE(MessageAircraftFlaps, flaps);
        HANDLE_DOUBLE(MessageAircraftThrottle, throttle);
        HANDLE_DOUBLE(MessageAircraftParkingBrake, parking_brake);

        // Engine State
        HANDLE_DOUBLE(MessageAircraftEngineRunning1, engine_running_1);
        HANDLE_DOUBLE(MessageAircraftEngineRunning2, engine_running_2);
        HANDLE_DOUBLE(MessageAircraftEngineThrottle1, engine_throttle_1);
        HANDLE_DOUBLE(MessageAircraftEngineThrottle2, engine_throttle_2);

        // Navigation
        HANDLE_DOUBLE(MessageNavigationNAV1Frequency, nav1_frequency);
        HANDLE_DOUBLE(MessageNavigationNAV2Frequency, nav2_frequency);
        HANDLE_DOUBLE(MessageCommunicationCOM1Frequency, com1_frequency);
        HANDLE_DOUBLE(MessageCommunicationCOM2Frequency, com2_frequency);
        HANDLE_DOUBLE(MessageNavigationSelectedCourse1, selected_course_1);
        HANDLE_DOUBLE(MessageNavigationSelectedCourse2, selected_course_2);

        // Autopilot
        HANDLE_DOUBLE(MessageAutopilotMaster, autopilot_master);
        HANDLE_DOUBLE(MessageAutopilotHeading, autopilot_heading);
        HANDLE_DOUBLE(MessageAutopilotSelectedAltitude, autopilot_altitude);
        HANDLE_DOUBLE(MessageAutopilotSelectedVerticalSpeed, autopilot_vertical_speed);
        HANDLE_DOUBLE(MessageAutopilotSelectedSpeed, autopilot_speed);

        // V-Speeds
        HANDLE_DOUBLE(MessagePerformanceSpeedVS0, vs0);
        HANDLE_DOUBLE(MessagePerformanceSpeedVS1, vs1);
        HANDLE_DOUBLE(MessagePerformanceSpeedVFE, vfe);
        HANDLE_DOUBLE(MessagePerformanceSpeedVNO, vno);
        HANDLE_DOUBLE(MessagePerformanceSpeedVNE, vne);

        // Nearest Airport
        HANDLE_DOUBLE(MessageAircraftNearestAirportElevation, nearest_airport_elevation);
        HANDLE_VECTOR2D(MessageAircraftNearestAirportLocation, nearest_airport_location);

        // String Variables (require special handling)
        message_handlers[MessageAircraftName.GetID()] = [this](const auto& msg) {
            ProcessStringMessage(msg, pData->aircraft_name, sizeof(pData->aircraft_name), "Unknown");
        };
        message_handlers[MessageAircraftNearestAirportIdentifier.GetID()] = [this](const auto& msg) {
            ProcessStringMessage(msg, pData->nearest_airport_id, sizeof(pData->nearest_airport_id), "----");
        };
        message_handlers[MessageAircraftNearestAirportName.GetID()] = [this](const auto& msg) {
            ProcessStringMessage(msg, pData->nearest_airport_name, sizeof(pData->nearest_airport_name), "Unknown");
        };
    }

    bool Initialize() {
        if (initialized) return true;

        // Create shared memory
        const char* sharedMemName = "AeroflyReaderData";

        hMapFile = CreateFileMappingA(
            INVALID_HANDLE_VALUE,
            NULL,
            PAGE_READWRITE,
            0,
            sizeof(AeroflyReaderData),
            sharedMemName
        );

        if (hMapFile == NULL) {
            DBG("Failed to create shared memory\n");
            return false;
        }

        pData = (AeroflyReaderData*)MapViewOfFile(
            hMapFile,
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            sizeof(AeroflyReaderData)
        );

        if (pData == NULL) {
            CloseHandle(hMapFile);
            hMapFile = NULL;
            DBG("Failed to map shared memory\n");
            return false;
        }

        // Initialize data structure
        memset(pData, 0, sizeof(AeroflyReaderData));
        pData->data_valid = 0;
        pData->update_counter = 0;
        strncpy_s(pData->aircraft_name, sizeof(pData->aircraft_name), "Unknown", _TRUNCATE);
        strncpy_s(pData->nearest_airport_id, sizeof(pData->nearest_airport_id), "----", _TRUNCATE);
        strncpy_s(pData->nearest_airport_name, sizeof(pData->nearest_airport_name), "Unknown", _TRUNCATE);

        initialized = true;
        DBG("SharedMemoryReader initialized\n");
        return true;
    }

    void UpdateData(const std::vector<tm_external_message>& messages) {
        if (!pData) return;

        std::lock_guard<std::mutex> lock(data_mutex);

        // Mark data as invalid during update
        pData->data_valid = 0;

        // Process all messages using hash map lookup
        for (const auto& msg : messages) {
            auto it = message_handlers.find(msg.GetID());
            if (it != message_handlers.end()) {
                it->second(msg);
            }
        }

        // Update timestamp and counter
        pData->timestamp_us = get_time_us();
        pData->update_counter++;
        pData->data_valid = 1;
    }

    void Cleanup() {
        if (pData) {
            UnmapViewOfFile(pData);
            pData = nullptr;
        }
        if (hMapFile) {
            CloseHandle(hMapFile);
            hMapFile = NULL;
        }
        initialized = false;
    }

    AeroflyReaderData* GetData() { return pData; }
    bool IsInitialized() const { return initialized; }
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// TCP DATA SERVER (Streaming Only - No Commands)
///////////////////////////////////////////////////////////////////////////////////////////////////

class TCPDataServer {
private:
    SOCKET server_socket;
    std::vector<SOCKET> client_sockets;
    std::thread server_thread;
    std::atomic<bool> running;
    mutable std::mutex clients_mutex;
    bool winsock_initialized;
    uint64_t last_broadcast_us;
    double current_hz;  // Calculated update rate

public:
    TCPDataServer() : server_socket(INVALID_SOCKET), running(false),
                      winsock_initialized(false), last_broadcast_us(0), current_hz(0.0) {}

    ~TCPDataServer() {
        Stop();
    }

    bool Start(int port = 12345) {
        // Initialize Winsock
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            return false;
        }
        winsock_initialized = true;

        // Create server socket
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == INVALID_SOCKET) {
            WSACleanup();
            winsock_initialized = false;
            return false;
        }

        // Allow socket reuse
        int opt = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

        // Bind to port
        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);

        if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            closesocket(server_socket);
            WSACleanup();
            winsock_initialized = false;
            return false;
        }

        // Listen for connections
        if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
            closesocket(server_socket);
            WSACleanup();
            winsock_initialized = false;
            return false;
        }

        // Start server thread
        running = true;
        server_thread = std::thread(&TCPDataServer::ServerLoop, this);

        DBG("TCPDataServer started on port 12345\n");
        return true;
    }

    void Stop() {
        running = false;

        if (server_socket != INVALID_SOCKET) {
            shutdown(server_socket, SD_BOTH);
            closesocket(server_socket);
            server_socket = INVALID_SOCKET;
        }

        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (SOCKET client : client_sockets) {
                shutdown(client, SD_BOTH);
                closesocket(client);
            }
            client_sockets.clear();
        }

        if (server_thread.joinable()) {
            server_thread.join();
        }

        if (winsock_initialized) {
            WSACleanup();
            winsock_initialized = false;
        }

        DBG("TCPDataServer stopped\n");
    }

    void BroadcastData(const AeroflyReaderData* data) {
        if (!data || !running) return;

        // Throttle broadcast rate
        const uint64_t now_us = get_time_us();
        static uint32_t broadcast_interval_ms = []() -> uint32_t {
            char buf[32] = {0};
            DWORD n = GetEnvironmentVariableA("AEROFLY_READER_BROADCAST_MS", buf, sizeof(buf));
            int ms = (n > 0) ? atoi(buf) : 20;
            if (ms < 5) ms = 5;
            return static_cast<uint32_t>(ms);
        }();

        uint64_t delta_us = now_us - last_broadcast_us;
        if (delta_us < static_cast<uint64_t>(broadcast_interval_ms) * 1000ULL) {
            return;
        }

        // Calculate actual update rate
        if (delta_us > 0) {
            current_hz = 1000000.0 / static_cast<double>(delta_us);
        }
        last_broadcast_us = now_us;

        // Build JSON (uses static buffer, returns pointer + length)
        int json_len = 0;
        const char* json_data = BuildDataJSON(data, json_len, current_hz);

        // Snapshot sockets
        std::vector<SOCKET> sockets_snapshot;
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            sockets_snapshot = client_sockets;
        }

        // Send to all clients
        std::vector<SOCKET> to_remove;
        for (SOCKET s : sockets_snapshot) {
            int result = send(s, json_data, json_len, 0);
            if (result == SOCKET_ERROR) {
                int err = WSAGetLastError();
                if (err != WSAEWOULDBLOCK) {
                    to_remove.push_back(s);
                }
            }
        }

        // Remove failed clients
        if (!to_remove.empty()) {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (SOCKET s : to_remove) {
                auto it = std::find(client_sockets.begin(), client_sockets.end(), s);
                if (it != client_sockets.end()) {
                    closesocket(*it);
                    client_sockets.erase(it);
                }
            }
        }
    }

    int GetClientCount() const {
        std::lock_guard<std::mutex> lock(clients_mutex);
        return static_cast<int>(client_sockets.size());
    }

private:
    void ServerLoop() {
        DBG("ServerLoop started\n");

        while (running) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(server_socket, &readfds);

            struct timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;

            int result = select(0, &readfds, NULL, NULL, &timeout);

            if (result > 0 && FD_ISSET(server_socket, &readfds)) {
                SOCKET client_socket = accept(server_socket, nullptr, nullptr);
                if (client_socket != INVALID_SOCKET && running) {
                    // Set non-blocking
                    u_long mode = 1;
                    ioctlsocket(client_socket, FIONBIO, &mode);

                    // Low-latency
                    int yes = 1;
                    setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(yes));

                    std::lock_guard<std::mutex> lock(clients_mutex);
                    client_sockets.push_back(client_socket);
                    DBG("Client connected\n");
                }
            }
        }

        DBG("ServerLoop finished\n");
    }
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// JSON BUILDER (optimized with snprintf + static buffer)
///////////////////////////////////////////////////////////////////////////////////////////////////

static thread_local char g_json_buffer[4096];

static const char* BuildDataJSON(const AeroflyReaderData* data, int& out_len, double update_hz = 0.0) {
    if (!data) { out_len = 3; return "{}\n"; }

    out_len = snprintf(g_json_buffer, sizeof(g_json_buffer),
        "{\"schema\":\"aerofly-reader-telemetry\",\"version\":\"" AEROFLY_READER_VERSION "\","
        "\"update_hz\":%.1f,\"timestamp\":%llu,\"data_valid\":%u,\"update_counter\":%u,",
        update_hz, (unsigned long long)data->timestamp_us, data->data_valid, data->update_counter);

    // Continue building JSON (append to buffer)
    out_len += snprintf(g_json_buffer + out_len, sizeof(g_json_buffer) - out_len,
        "\"latitude\":%.6f,\"longitude\":%.6f,\"altitude\":%.2f,\"height\":%.2f,"
        "\"pitch\":%.6f,\"bank\":%.6f,\"true_heading\":%.6f,\"magnetic_heading\":%.6f,"
        "\"indicated_airspeed\":%.2f,\"ground_speed\":%.2f,\"vertical_speed\":%.3f,"
        "\"mach_number\":%.4f,\"angle_of_attack\":%.6f,"
        "\"on_ground\":%.0f,\"gear\":%.2f,\"flaps\":%.2f,\"throttle\":%.2f,\"parking_brake\":%.0f,"
        "\"engine_running_1\":%.0f,\"engine_running_2\":%.0f,"
        "\"engine_throttle_1\":%.2f,\"engine_throttle_2\":%.2f,"
        "\"nav1_frequency\":%.3f,\"nav2_frequency\":%.3f,"
        "\"com1_frequency\":%.3f,\"com2_frequency\":%.3f,"
        "\"autopilot_master\":%.0f,\"autopilot_heading\":%.6f,\"autopilot_altitude\":%.0f,"
        "\"vs0\":%.2f,\"vs1\":%.2f,\"vfe\":%.2f,\"vno\":%.2f,\"vne\":%.2f,"
        "\"position\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f},"
        "\"velocity\":{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f},"
        "\"aircraft_name\":\"%s\",\"nearest_airport_id\":\"%s\",\"nearest_airport_name\":\"%s\"}\n",
        data->latitude, data->longitude, data->altitude, data->height,
        data->pitch, data->bank, data->true_heading, data->magnetic_heading,
        data->indicated_airspeed, data->ground_speed, data->vertical_speed,
        data->mach_number, data->angle_of_attack,
        data->on_ground, data->gear, data->flaps, data->throttle, data->parking_brake,
        data->engine_running_1, data->engine_running_2,
        data->engine_throttle_1, data->engine_throttle_2,
        data->nav1_frequency, data->nav2_frequency,
        data->com1_frequency, data->com2_frequency,
        data->autopilot_master, data->autopilot_heading, data->autopilot_altitude,
        data->vs0, data->vs1, data->vfe, data->vno, data->vne,
        data->position.x, data->position.y, data->position.z,
        data->velocity.x, data->velocity.y, data->velocity.z,
        data->aircraft_name, data->nearest_airport_id, data->nearest_airport_name);

    return g_json_buffer;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// AEROFLY READER - Main Orchestrator
///////////////////////////////////////////////////////////////////////////////////////////////////

class AeroflyReader {
private:
    std::unique_ptr<SharedMemoryReader> shared_memory;
    std::unique_ptr<TCPDataServer> tcp_server;
    bool initialized;

public:
    AeroflyReader() : initialized(false) {}

    ~AeroflyReader() {
        Shutdown();
    }

    bool Initialize() {
        if (initialized) return true;

        DBG("=== AeroflyReader Initializing ===\n");

        // Initialize shared memory
        shared_memory = std::make_unique<SharedMemoryReader>();
        if (!shared_memory->Initialize()) {
            DBG("Failed to initialize SharedMemoryReader\n");
            return false;
        }

        // Initialize TCP server
        tcp_server = std::make_unique<TCPDataServer>();

        // Read port from environment or use default
        int tcp_port = 12345;
        char buf[32] = {0};
        if (GetEnvironmentVariableA("AEROFLY_READER_TCP_PORT", buf, sizeof(buf)) > 0) {
            tcp_port = atoi(buf);
            if (tcp_port < 1024 || tcp_port > 65535) tcp_port = 12345;
        }

        if (!tcp_server->Start(tcp_port)) {
            DBG("Failed to start TCPDataServer\n");
            // Continue without TCP - shared memory still works
        }

        initialized = true;
        DBG("=== AeroflyReader Initialized ===\n");
        return true;
    }

    void Update(const std::vector<tm_external_message>& received_messages) {
        if (!initialized || !shared_memory) return;

        // Update shared memory with received data
        shared_memory->UpdateData(received_messages);

        // Broadcast to TCP clients
        if (tcp_server) {
            tcp_server->BroadcastData(shared_memory->GetData());
        }
    }

    void Shutdown() {
        if (!initialized) return;

        DBG("=== AeroflyReader Shutting Down ===\n");

        if (tcp_server) {
            tcp_server->Stop();
            tcp_server.reset();
        }

        if (shared_memory) {
            shared_memory->Cleanup();
            shared_memory.reset();
        }

        initialized = false;
        DBG("=== AeroflyReader Shutdown Complete ===\n");
    }

    bool IsInitialized() const { return initialized; }
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// GLOBAL INSTANCE
///////////////////////////////////////////////////////////////////////////////////////////////////

static std::unique_ptr<AeroflyReader> g_reader;

///////////////////////////////////////////////////////////////////////////////////////////////////
// DLL ENTRY POINTS (Aerofly SDK Interface)
///////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" {

/**
 * @brief Returns the DLL interface version for Aerofly compatibility
 */
__declspec(dllexport) int Aerofly_FS_4_External_DLL_GetInterfaceVersion() {
    return TM_DLL_INTERFACE_VERSION;
}

/**
 * @brief Initializes the reader DLL
 * Called once when Aerofly loads the DLL
 */
__declspec(dllexport) int Aerofly_FS_4_External_DLL_Init(HINSTANCE hInstance) {
    DBG("=== Aerofly Reader DLL Init ===\n");

    g_reader = std::make_unique<AeroflyReader>();

    if (!g_reader->Initialize()) {
        DBG("Failed to initialize AeroflyReader\n");
        g_reader.reset();
        return 0;
    }

    return 1;
}

/**
 * @brief Shuts down the reader DLL
 * Called when Aerofly unloads the DLL
 */
__declspec(dllexport) void Aerofly_FS_4_External_DLL_Shutdown() {
    DBG("=== Aerofly Reader DLL Shutdown ===\n");

    if (g_reader) {
        g_reader->Shutdown();
        g_reader.reset();
    }
}

/**
 * @brief Main update function called by Aerofly 50-60 times per second
 *
 * This simplified version:
 * - Parses incoming messages from the simulator
 * - Updates shared memory
 * - Broadcasts to TCP clients
 * - Does NOT send any messages back (read-only)
 *
 * @param delta_time Time since last update
 * @param received_bytes Byte stream of messages from Aerofly
 * @param received_size Size of received byte stream
 * @param sent_messages Vector to store outgoing messages (unused in read-only mode)
 */
__declspec(dllexport) void Aerofly_FS_4_External_DLL_Update(
    double delta_time,
    const char* received_bytes,
    int received_size,
    std::vector<tm_external_message>& sent_messages)
{
    if (!g_reader || !g_reader->IsInitialized()) return;

    // Parse incoming messages
    std::vector<tm_external_message> received_messages;
    int offset = 0;

    while (offset < received_size) {
        tm_external_message msg;
        int bytes_read = msg.GetFromByteStream(received_bytes + offset, received_size - offset);

        if (bytes_read <= 0) break;

        received_messages.push_back(msg);
        offset += bytes_read;
    }

    // Update reader (shared memory + TCP broadcast)
    g_reader->Update(received_messages);

    // NOTE: sent_messages is left empty - this is a READ-ONLY implementation
    // No commands are sent back to the simulator
}

} // extern "C"
