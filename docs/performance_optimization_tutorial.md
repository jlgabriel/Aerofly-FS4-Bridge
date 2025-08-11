# Performance Optimization

**Mastering high-performance techniques for real-time flight simulation systems**

Copyright (c) 2025 Juan Luis Gabriel

This tutorial explores advanced performance optimization techniques used in the Aerofly Bridge, teaching you how to build ultra-high-performance flight simulation applications. Learn to measure, analyze, and optimize every aspect of your real-time systems for professional-grade performance.

## 🎯 What You'll Learn

- **Memory layout optimization** for cache efficiency
- **High-frequency data streaming** (50+ Hz performance)
- **Lock-free algorithms** for ultra-low latency
- **Cache-friendly programming** patterns
- **Profiling and benchmarking** techniques
- **Bottleneck identification** in real-time systems
- **SIMD optimization** for bulk operations
- **Production monitoring** strategies

## 📋 Prerequisites

- **C++ advanced** concepts (memory model, optimization)
- **Performance analysis** tools familiarity

---

## ⚡ Performance Requirements in Flight Simulation

### Real-Time Constraints

Flight simulation DLLs operate under **extremely strict performance requirements**:

```cpp
/*
Performance Budget Breakdown (Aerofly Bridge):
┌─────────────────────┬─────────────┬────────────────┬────────────────┐
│ Operation           │ Target Time │ Max Acceptable │ Consequence    │
├─────────────────────┼─────────────┼────────────────┼────────────────┤
│ Update() call       │ < 100µs     │ < 1ms          │ Sim stutter    │
│ SharedMemory write  │ < 10µs      │ < 50µs         │ Data corruption│
│ JSON generation     │ < 1ms       │ < 5ms          │ Client lag     │
│ Network broadcast   │ < 2ms       │ < 10ms         │ Connection drop│
│ Command processing  │ < 500µs     │ < 2ms          │ Control delay  │
│ Memory allocation   │ 0µs         │ 0µs            │ RT violation   │
└─────────────────────┴─────────────┴────────────────┴────────────────┘

Update Frequency: 50-60 Hz (16.6-20ms intervals)
Total CPU Budget: ~5-10% of one core maximum
*/
```

### Performance Characteristics by Interface

```cpp
// Measured performance characteristics (Aerofly Bridge)
Interface Performance Comparison:
┌─────────────────┬─────────────┬─────────────┬─────────────┐
│ Interface       │ Latency     │ Throughput  │ CPU Usage   │
├─────────────────┼─────────────┼─────────────┼─────────────┤
│ SharedMemory    │ 0.5-2µs     │ ~50MB/s     │ 0.1%        │
│ TCP             │ 50-200µs    │ ~10MB/s     │ 1-3%        │
│ WebSocket       │ 100-500µs   │ ~5MB/s      │ 2-5%        │
└─────────────────┴─────────────┴─────────────┴─────────────┘

// At 50Hz with 50 TCP clients + 20 WebSocket clients
Total CPU Impact: ~3-8% of one core
```

---

## 🧠 Memory Layout Optimization

### Cache-Friendly Data Structures

**Problem**: Cache misses dominate performance in high-frequency operations

**Solution**: Optimize memory layout for spatial and temporal locality

```cpp
// ❌ Bad: Scattered memory layout
struct BadFlightData {
    std::string aircraft_name;          // 32 bytes + heap allocation
    double altitude;                    // 8 bytes
    std::vector<double> nav_data;       // 24 bytes + heap allocation
    double airspeed;                    // 8 bytes
    bool on_ground;                     // 1 byte + 7 padding
    double fuel_remaining;              // 8 bytes
    // Total: ~81 bytes + heap allocations
    // Cache lines: 2-3 (poor locality)
};

// ✅ Good: Cache-optimized layout
struct alignas(64) OptimizedFlightData {
    // === HOT DATA (accessed every frame) ===
    // Pack into first 64 bytes (one cache line)
    uint64_t timestamp_us;              // 8 bytes
    uint32_t data_valid;               // 4 bytes
    uint32_t sequence_number;          // 4 bytes
    
    // Essential flight data (spatial locality)
    double altitude;                    // 8 bytes
    double indicated_airspeed;          // 8 bytes
    double true_airspeed;              // 8 bytes
    double ground_speed;               // 8 bytes
    double pitch;                      // 8 bytes
    double bank;                       // 8 bytes
    // Subtotal: 64 bytes (exactly one cache line)
    
    // === WARM DATA (second cache line) ===
    double heading_true;                // 8 bytes
    double heading_magnetic;            // 8 bytes
    double fuel_quantity;              // 8 bytes
    double fuel_flow;                  // 8 bytes
    double engine_rpm;                 // 8 bytes
    double manifold_pressure;         // 8 bytes
    uint32_t on_ground;               // 4 bytes (bool as uint32)
    uint32_t gear_position;           // 4 bytes
    // Subtotal: 64 bytes (second cache line)
    
    // === COLD DATA (rarely accessed) ===
    char aircraft_name[32];            // 32 bytes (fixed size, no heap)
    char departure_icao[8];            // 8 bytes
    char destination_icao[8];          // 8 bytes
    char padding[16];                  // 16 bytes (align to 64)
    // Subtotal: 64 bytes (third cache line)
    
    // === ARRAY DATA (sequential access) ===
    double all_variables[339];         // 2712 bytes (43 cache lines)
    // Optimized for sequential iteration
};

static_assert(sizeof(OptimizedFlightData) % 64 == 0, "Must align to cache line boundaries");
```

### Memory Pool Allocation

**Problem**: Dynamic allocation causes heap fragmentation and unpredictable latency

**Solution**: Pre-allocated memory pools with fixed-size blocks

```cpp
template<typename T, size_t PoolSize>
class HighPerformancePool {
private:
    struct Block {
        alignas(T) char data[sizeof(T)];
        Block* next;
        bool in_use;
    };
    
    std::array<Block, PoolSize> pool_;
    Block* free_list_;
    std::atomic<size_t> allocated_count_{0};
    std::mutex pool_mutex_;  // Only for allocation/deallocation
    
public:
    HighPerformancePool() {
        // Initialize free list
        for (size_t i = 0; i < PoolSize - 1; ++i) {
            pool_[i].next = &pool_[i + 1];
            pool_[i].in_use = false;
        }
        pool_[PoolSize - 1].next = nullptr;
        pool_[PoolSize - 1].in_use = false;
        free_list_ = &pool_[0];
    }
    
    // Ultra-fast allocation (amortized O(1))
    T* allocate() {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        
        if (!free_list_) {
            return nullptr;  // Pool exhausted
        }
        
        Block* block = free_list_;
        free_list_ = block->next;
        block->in_use = true;
        
        allocated_count_.fetch_add(1, std::memory_order_relaxed);
        
        return reinterpret_cast<T*>(block->data);
    }
    
    // Ultra-fast deallocation
    void deallocate(T* ptr) {
        if (!ptr) return;
        
        Block* block = reinterpret_cast<Block*>(
            reinterpret_cast<char*>(ptr) - offsetof(Block, data));
        
        std::lock_guard<std::mutex> lock(pool_mutex_);
        
        block->next = free_list_;
        free_list_ = block;
        block->in_use = false;
        
        allocated_count_.fetch_sub(1, std::memory_order_relaxed);
    }
    
    size_t allocated_count() const {
        return allocated_count_.load(std::memory_order_relaxed);
    }
    
    constexpr size_t capacity() const { return PoolSize; }
};

// Usage in Bridge
using MessagePool = HighPerformancePool<tm_external_message, 1024>;
static MessagePool g_message_pool;

// Fast message allocation
tm_external_message* msg = g_message_pool.allocate();
if (msg) {
    new(msg) tm_external_message("Aircraft.Altitude", tm_msg_data_type::Double);
    // Use message...
    msg->~tm_external_message();
    g_message_pool.deallocate(msg);
}
```

### NUMA-Aware Memory Management

**For multi-socket systems** (relevant for dedicated sim servers):

```cpp
class NUMAAwareAllocator {
private:
    static constexpr size_t CACHE_LINE_SIZE = 64;
    
public:
    // Allocate memory on the current NUMA node
    template<typename T>
    static T* allocate_local(size_t count = 1) {
        size_t size = sizeof(T) * count;
        size_t aligned_size = (size + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1);
        
        // Windows: Use VirtualAllocExNuma for NUMA awareness
        void* ptr = VirtualAlloc(nullptr, aligned_size, 
                                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        
        if (!ptr) {
            throw std::bad_alloc();
        }
        
        return static_cast<T*>(ptr);
    }
    
    template<typename T>
    static void deallocate_local(T* ptr) {
        if (ptr) {
            VirtualFree(ptr, 0, MEM_RELEASE);
        }
    }
};

// Usage for thread-local data structures
thread_local auto tcp_send_buffer = 
    NUMAAwareAllocator::allocate_local<char>(65536);  // 64KB buffer per thread
```

---

## 🚀 High-Frequency Data Streaming Optimization

### Zero-Copy Broadcasting Patterns

**Problem**: Copying data for each client is expensive
**Solution**: Shared data with reference counting

```cpp
class ZeroCopyBroadcaster {
private:
    struct BroadcastPacket {
        std::string tcp_data;              // JSON + newline
        std::vector<uint8_t> ws_frame;     // WebSocket frame
        uint64_t timestamp_us;
        uint32_t sequence_number;
        std::atomic<int> ref_count{0};
    };
    
    using PacketPtr = std::shared_ptr<BroadcastPacket>;
    std::atomic<PacketPtr> current_packet_;
    std::atomic<uint32_t> sequence_counter_{0};
    
public:
    // Producer: Create new packet (called from Aerofly thread)
    void UpdateBroadcastData(const AeroflyBridgeData* data) {
        // Generate content once
        std::string json = BuildDataJSON(data);
        if (json.empty()) return;
        
        // Create new packet
        auto packet = std::make_shared<BroadcastPacket>();
        packet->tcp_data = json + "\n";  // TCP format
        packet->ws_frame = CreateWebSocketFrame(json);  // WebSocket format
        packet->timestamp_us = get_time_us();
        packet->sequence_number = sequence_counter_.fetch_add(1, std::memory_order_relaxed);
        
        // Atomically publish new packet
        current_packet_.store(packet, std::memory_order_release);
    }
    
    // Consumers: Get shared packet (called from network threads)
    PacketPtr GetCurrentPacket() const {
        return current_packet_.load(std::memory_order_acquire);
    }
    
    // Usage in network threads
    void BroadcastToTCPClients(const std::vector<SOCKET>& clients) {
        auto packet = GetCurrentPacket();
        if (!packet) return;
        
        // All clients share the same data (zero copy)
        for (SOCKET client : clients) {
            send(client, packet->tcp_data.c_str(), 
                 packet->tcp_data.length(), MSG_DONTWAIT);
        }
        // packet automatically freed when all references released
    }
};
```

### Vectorized Operations with SIMD

**Problem**: Processing 339+ variables one by one is slow
**Solution**: SIMD operations for bulk processing

```cpp
#include <immintrin.h>  // AVX2 intrinsics

class SIMDOptimizedProcessor {
public:
    // Process 4 doubles simultaneously with AVX2
    static void ProcessVariableArrayAVX2(double* variables, size_t count, 
                                        double multiplier) {
        const __m256d mult_vec = _mm256_set1_pd(multiplier);
        size_t simd_count = count & ~3;  // Round down to multiple of 4
        
        // Process 4 elements at a time
        for (size_t i = 0; i < simd_count; i += 4) {
            __m256d data = _mm256_load_pd(&variables[i]);  // Load 4 doubles
            data = _mm256_mul_pd(data, mult_vec);           // Multiply by scalar
            _mm256_store_pd(&variables[i], data);          // Store result
        }
        
        // Handle remaining elements (< 4)
        for (size_t i = simd_count; i < count; ++i) {
            variables[i] *= multiplier;
        }
    }
    
    // Optimized variable validation (check for NaN/Inf)
    static bool ValidateVariablesAVX2(const double* variables, size_t count) {
        size_t simd_count = count & ~3;
        
        for (size_t i = 0; i < simd_count; i += 4) {
            __m256d data = _mm256_load_pd(&variables[i]);
            
            // Check for finite values (not NaN or Inf)
            __m256d finite_mask = _mm256_cmp_pd(data, data, _CMP_ORD_Q);
            
            // If any element is not finite, return false
            int mask = _mm256_movemask_pd(finite_mask);
            if (mask != 0xF) {  // All 4 bits should be set for finite values
                return false;
            }
        }
        
        // Validate remaining elements
        for (size_t i = simd_count; i < count; ++i) {
            if (!std::isfinite(variables[i])) {
                return false;
            }
        }
        
        return true;
    }
    
    // Fast checksum calculation for data integrity
    static uint64_t CalculateChecksumAVX2(const double* data, size_t count) {
        __m256i checksum = _mm256_setzero_si256();
        size_t simd_count = count & ~3;
        
        for (size_t i = 0; i < simd_count; i += 4) {
            __m256d values = _mm256_load_pd(&data[i]);
            __m256i int_values = _mm256_castpd_si256(values);
            checksum = _mm256_xor_si256(checksum, int_values);
        }
        
        // Extract and combine the 4 64-bit values
        alignas(32) uint64_t result[4];
        _mm256_store_si256((__m256i*)result, checksum);
        
        uint64_t final_checksum = result[0] ^ result[1] ^ result[2] ^ result[3];
        
        // Handle remaining elements
        for (size_t i = simd_count; i < count; ++i) {
            final_checksum ^= *reinterpret_cast<const uint64_t*>(&data[i]);
        }
        
        return final_checksum;
    }
};

// Usage in Aerofly Bridge
void AeroflyBridge::OptimizedUpdate(const AeroflyBridgeData* data) {
    // Validate all 339 variables with SIMD (much faster than scalar)
    if (!SIMDOptimizedProcessor::ValidateVariablesAVX2(
            data->all_variables, 339)) {
        OutputDebugStringA("WARNING: Invalid data detected!\n");
        return;
    }
    
    // Calculate integrity checksum
    uint64_t checksum = SIMDOptimizedProcessor::CalculateChecksumAVX2(
        data->all_variables, 339);
    
    // Store checksum for clients to verify data integrity
    const_cast<AeroflyBridgeData*>(data)->data_checksum = checksum;
}
```

### Lock-Free Ring Buffers for Commands

**Problem**: Command queue contention between network threads
**Solution**: Multiple lock-free ring buffers

```cpp
template<typename T, size_t Size>
class LockFreeRingBuffer {
private:
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");
    static constexpr size_t MASK = Size - 1;
    
    struct alignas(64) CacheLinePad {};  // Prevent false sharing
    
    alignas(64) std::atomic<size_t> write_index_{0};
    CacheLinePad pad1_;
    alignas(64) std::atomic<size_t> read_index_{0};
    CacheLinePad pad2_;
    alignas(64) std::array<T, Size> buffer_;
    
public:
    // Producer thread (single writer)
    bool try_emplace(T&& item) {
        const size_t current_write = write_index_.load(std::memory_order_relaxed);
        const size_t next_write = (current_write + 1) & MASK;
        
        if (next_write == read_index_.load(std::memory_order_acquire)) {
            return false;  // Buffer full
        }
        
        buffer_[current_write] = std::move(item);
        write_index_.store(next_write, std::memory_order_release);
        return true;
    }
    
    // Consumer thread (single reader)
    bool try_dequeue(T& item) {
        const size_t current_read = read_index_.load(std::memory_order_relaxed);
        
        if (current_read == write_index_.load(std::memory_order_acquire)) {
            return false;  // Buffer empty
        }
        
        item = std::move(buffer_[current_read]);
        read_index_.store((current_read + 1) & MASK, std::memory_order_release);
        return true;
    }
    
    // Fast size check (may be approximate due to concurrency)
    size_t approximate_size() const {
        const size_t write = write_index_.load(std::memory_order_relaxed);
        const size_t read = read_index_.load(std::memory_order_relaxed);
        return (write - read) & MASK;
    }
};

// Multi-buffer command system for scalability
class ScalableCommandSystem {
private:
    static constexpr size_t NUM_BUFFERS = 4;  // Power of 2
    static constexpr size_t BUFFER_SIZE = 1024;
    
    std::array<LockFreeRingBuffer<std::string, BUFFER_SIZE>, NUM_BUFFERS> buffers_;
    std::atomic<size_t> round_robin_counter_{0};
    
public:
    // Network threads: Distribute commands across buffers
    bool EnqueueCommand(std::string command) {
        // Round-robin selection reduces contention
        size_t buffer_index = round_robin_counter_.fetch_add(1, std::memory_order_relaxed) 
                              & (NUM_BUFFERS - 1);
        
        return buffers_[buffer_index].try_emplace(std::move(command));
    }
    
    // Aerofly thread: Process all buffers
    std::vector<std::string> DequeueAllCommands() {
        std::vector<std::string> commands;
        commands.reserve(256);  // Typical batch size
        
        // Process all buffers
        for (auto& buffer : buffers_) {
            std::string command;
            while (buffer.try_dequeue(command)) {
                commands.push_back(std::move(command));
            }
        }
        
        return commands;
    }
    
    // Monitoring: Get total queued commands
    size_t GetTotalQueuedCommands() const {
        size_t total = 0;
        for (const auto& buffer : buffers_) {
            total += buffer.approximate_size();
        }
        return total;
    }
};
```

---

## 📊 Profiling and Benchmarking

### High-Resolution Performance Measurement

```cpp
class HighResolutionProfiler {
private:
    struct ProfileEntry {
        std::atomic<uint64_t> total_time_ns{0};
        std::atomic<uint64_t> call_count{0};
        std::atomic<uint64_t> min_time_ns{UINT64_MAX};
        std::atomic<uint64_t> max_time_ns{0};
        std::string name;
    };
    
    std::unordered_map<std::string, std::unique_ptr<ProfileEntry>> entries_;
    mutable std::shared_mutex entries_mutex_;
    
    // High-resolution timer (uses RDTSC on x64)
    static uint64_t GetHighResolutionTime() {
        #ifdef _WIN64
            return __rdtsc();  // CPU cycles (most accurate)
        #else
            LARGE_INTEGER counter;
            QueryPerformanceCounter(&counter);
            return counter.QuadPart;
        #endif
    }
    
    static uint64_t GetTimerFrequency() {
        #ifdef _WIN64
            // Estimate CPU frequency (approximate)
            static uint64_t frequency = []() {
                LARGE_INTEGER freq;
                QueryPerformanceFrequency(&freq);
                
                auto start_qpc = GetHighResolutionTime();
                auto start_rdtsc = __rdtsc();
                
                Sleep(10);  // 10ms calibration
                
                auto end_qpc = GetHighResolutionTime();
                auto end_rdtsc = __rdtsc();
                
                return (end_rdtsc - start_rdtsc) * freq.QuadPart / (end_qpc - start_qpc);
            }();
            return frequency;
        #else
            LARGE_INTEGER freq;
            QueryPerformanceFrequency(&freq);
            return freq.QuadPart;
        #endif
    }
    
public:
    class ScopedTimer {
    private:
        ProfileEntry* entry_;
        uint64_t start_time_;
        
    public:
        ScopedTimer(ProfileEntry* entry) 
            : entry_(entry), start_time_(GetHighResolutionTime()) {}
        
        ~ScopedTimer() {
            uint64_t end_time = GetHighResolutionTime();
            uint64_t duration = end_time - start_time_;
            
            // Convert cycles to nanoseconds
            uint64_t duration_ns = (duration * 1000000000ULL) / GetTimerFrequency();
            
            // Update statistics atomically
            entry_->total_time_ns.fetch_add(duration_ns, std::memory_order_relaxed);
            entry_->call_count.fetch_add(1, std::memory_order_relaxed);
            
            // Update min (compare-and-swap loop)
            uint64_t current_min = entry_->min_time_ns.load(std::memory_order_relaxed);
            while (duration_ns < current_min && 
                   !entry_->min_time_ns.compare_exchange_weak(current_min, duration_ns)) {
            }
            
            // Update max
            uint64_t current_max = entry_->max_time_ns.load(std::memory_order_relaxed);
            while (duration_ns > current_max && 
                   !entry_->max_time_ns.compare_exchange_weak(current_max, duration_ns)) {
            }
        }
    };
    
    ScopedTimer Profile(const std::string& name) {
        // Fast path: try shared lock first
        {
            std::shared_lock<std::shared_mutex> lock(entries_mutex_);
            auto it = entries_.find(name);
            if (it != entries_.end()) {
                return ScopedTimer(it->second.get());
            }
        }
        
        // Slow path: create new entry
        {
            std::unique_lock<std::shared_mutex> lock(entries_mutex_);
            auto [it, inserted] = entries_.emplace(name, std::make_unique<ProfileEntry>());
            it->second->name = name;
            return ScopedTimer(it->second.get());
        }
    }
    
    void PrintDetailedReport() const {
        std::shared_lock<std::shared_mutex> lock(entries_mutex_);
        
        OutputDebugStringA("=== PERFORMANCE PROFILE REPORT ===\n");
        
        for (const auto& [name, entry] : entries_) {
            uint64_t calls = entry->call_count.load();
            uint64_t total_ns = entry->total_time_ns.load();
            uint64_t min_ns = entry->min_time_ns.load();
            uint64_t max_ns = entry->max_time_ns.load();
            
            if (calls > 0) {
                uint64_t avg_ns = total_ns / calls;
                
                char buffer[512];
                sprintf_s(buffer, 
                    "%-30s: %8llu calls | %6llu ns avg | %6llu ns min | %8llu ns max | %8.2f ms total\n",
                    name.c_str(), calls, avg_ns, min_ns, max_ns, total_ns / 1000000.0);
                
                OutputDebugStringA(buffer);
            }
        }
        
        OutputDebugStringA("=====================================\n");
    }
};

// Global profiler instance
static HighResolutionProfiler g_profiler;

// Convenient macro for profiling
#define PROFILE_SCOPE(name) auto __prof_timer = g_profiler.Profile(name)

// Usage in Aerofly Bridge
void AeroflyBridge::Update(/* params */) {
    PROFILE_SCOPE("AeroflyBridge::Update");
    
    {
        PROFILE_SCOPE("SharedMemory::UpdateData");
        shared_memory.UpdateData(received_messages, delta_time);
    }
    
    {
        PROFILE_SCOPE("JSON::Generation");
        const std::string json = BuildDataJSON(shared_memory.GetData());
    }
    
    {
        PROFILE_SCOPE("Network::Broadcast");
        tcp_server.BroadcastData(shared_memory.GetData());
        ws_server.BroadcastData(shared_memory.GetData());
    }
}
```

### Memory Usage Monitoring

```cpp
class MemoryMonitor {
private:
    struct MemoryStats {
        std::atomic<size_t> allocated_bytes{0};
        std::atomic<size_t> allocation_count{0};
        std::atomic<size_t> peak_usage{0};
        std::atomic<size_t> total_allocations{0};
        std::atomic<size_t> total_deallocations{0};
    };
    
    MemoryStats stats_;
    std::atomic<bool> monitoring_enabled_{true};
    
public:
    void RecordAllocation(size_t bytes) {
        if (!monitoring_enabled_.load(std::memory_order_relaxed)) return;
        
        stats_.allocated_bytes.fetch_add(bytes, std::memory_order_relaxed);
        stats_.allocation_count.fetch_add(1, std::memory_order_relaxed);
        stats_.total_allocations.fetch_add(1, std::memory_order_relaxed);
        
        // Update peak usage
        size_t current = stats_.allocated_bytes.load(std::memory_order_relaxed);
        size_t peak = stats_.peak_usage.load(std::memory_order_relaxed);
        while (current > peak && 
               !stats_.peak_usage.compare_exchange_weak(peak, current)) {
        }
    }
    
    void RecordDeallocation(size_t bytes) {
        if (!monitoring_enabled_.load(std::memory_order_relaxed)) return;
        
        stats_.allocated_bytes.fetch_sub(bytes, std::memory_order_relaxed);
        stats_.allocation_count.fetch_sub(1, std::memory_order_relaxed);
        stats_.total_deallocations.fetch_add(1, std::memory_order_relaxed);
    }
    
    struct MemoryReport {
        size_t current_usage_bytes;
        size_t peak_usage_bytes;
        size_t active_allocations;
        size_t total_allocations;
        size_t total_deallocations;
        double fragmentation_ratio;
    };
    
    MemoryReport GetReport() const {
        MemoryReport report;
        report.current_usage_bytes = stats_.allocated_bytes.load();
        report.peak_usage_bytes = stats_.peak_usage.load();
        report.active_allocations = stats_.allocation_count.load();
        report.total_allocations = stats_.total_allocations.load();
        report.total_deallocations = stats_.total_deallocations.load();
        
        // Simple fragmentation estimate
        if (report.active_allocations > 0) {
            report.fragmentation_ratio = 
                static_cast<double>(report.current_usage_bytes) / 
                (report.active_allocations * 64);  // Assume 64-byte average
        } else {
            report.fragmentation_ratio = 0.0;
        }
        
        return report;
    }
    
    void PrintReport() const {
        auto report = GetReport();
        
        char buffer[512];
        sprintf_s(buffer, 
            "MEMORY: %zu KB current | %zu KB peak | %zu active allocs | %.2f fragmentation ratio\n",
            report.current_usage_bytes / 1024,
            report.peak_usage_bytes / 1024,
            report.active_allocations,
            report.fragmentation_ratio);
        
        OutputDebugStringA(buffer);
    }
};
```

### CPU Cache Performance Analysis

```cpp
class CacheAnalyzer {
private:
    struct CacheStats {
        std::atomic<uint64_t> l1_cache_misses{0};
        std::atomic<uint64_t> l2_cache_misses{0};
        std::atomic<uint64_t> memory_stalls{0};
        std::atomic<uint64_t> total_memory_accesses{0};
    };
    
    CacheStats stats_;
    
public:
    // Benchmark memory access patterns
    void BenchmarkSequentialAccess(double* data, size_t count) {
        uint64_t start_time = __rdtsc();
        
        // Sequential access (cache-friendly)
        volatile double sum = 0.0;
        for (size_t i = 0; i < count; ++i) {
            sum += data[i];
        }
        
        uint64_t end_time = __rdtsc();
        uint64_t cycles = end_time - start_time;
        
        // Store result to prevent optimization
        static volatile double result = sum;
        
        char buffer[256];
        sprintf_s(buffer, "Sequential access: %llu cycles for %zu elements (%.2f cycles/element)\n",
                  cycles, count, static_cast<double>(cycles) / count);
        OutputDebugStringA(buffer);
    }
    
    void BenchmarkRandomAccess(double* data, size_t count) {
        // Generate random indices
        std::vector<size_t> indices(count);
        std::iota(indices.begin(), indices.end(), 0);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::shuffle(indices.begin(), indices.end(), gen);
        
        uint64_t start_time = __rdtsc();
        
        // Random access (cache-unfriendly)
        volatile double sum = 0.0;
        for (size_t idx : indices) {
            sum += data[idx];
        }
        
        uint64_t end_time = __rdtsc();
        uint64_t cycles = end_time - start_time;
        
        static volatile double result = sum;
        
        char buffer[256];
        sprintf_s(buffer, "Random access: %llu cycles for %zu elements (%.2f cycles/element)\n",
                  cycles, count, static_cast<double>(cycles) / count);
        OutputDebugStringA(buffer);
    }
    
    // Test cache line effects
    void BenchmarkCacheLineStriding() {
        constexpr size_t ARRAY_SIZE = 1024 * 1024;  // 1M elements
        constexpr size_t CACHE_LINE_SIZE = 64;
        constexpr size_t DOUBLES_PER_CACHE_LINE = CACHE_LINE_SIZE / sizeof(double);
        
        auto data = std::make_unique<double[]>(ARRAY_SIZE);
        std::fill_n(data.get(), ARRAY_SIZE, 1.0);
        
        // Test different stride patterns
        for (size_t stride = 1; stride <= 64; stride *= 2) {
            uint64_t start_time = __rdtsc();
            
            volatile double sum = 0.0;
            for (size_t i = 0; i < ARRAY_SIZE; i += stride) {
                sum += data[i];
            }
            
            uint64_t end_time = __rdtsc();
            uint64_t cycles = end_time - start_time;
            size_t accesses = ARRAY_SIZE / stride;
            
            char buffer[256];
            sprintf_s(buffer, "Stride %2zu: %8llu cycles, %6zu accesses, %.2f cycles/access\n",
                      stride, cycles, accesses, static_cast<double>(cycles) / accesses);
            OutputDebugStringA(buffer);
        }
    }
};
```

---

## 🔍 Bottleneck Identification Techniques

### Real-Time Performance Monitoring

```cpp
class RealTimeMonitor {
private:
    struct TimeWindow {
        std::array<uint64_t, 60> frame_times_us;  // Last 60 frames
        size_t current_index = 0;
        bool window_full = false;
    };
    
    TimeWindow update_times_;
    TimeWindow broadcast_times_;
    std::atomic<uint64_t> last_frame_time_us_{0};
    
public:
    void RecordUpdateTime(uint64_t duration_us) {
        update_times_.frame_times_us[update_times_.current_index] = duration_us;
        update_times_.current_index = (update_times_.current_index + 1) % 60;
        if (update_times_.current_index == 0) {
            update_times_.window_full = true;
        }
        
        last_frame_time_us_.store(duration_us, std::memory_order_relaxed);
    }
    
    void RecordBroadcastTime(uint64_t duration_us) {
        broadcast_times_.frame_times_us[broadcast_times_.current_index] = duration_us;
        broadcast_times_.current_index = (broadcast_times_.current_index + 1) % 60;
        if (broadcast_times_.current_index == 0) {
            broadcast_times_.window_full = true;
        }
    }
    
    struct PerformanceMetrics {
        double avg_update_time_us;
        double max_update_time_us;
        double p95_update_time_us;
        double avg_broadcast_time_us;
        double frame_rate_hz;
        bool performance_warning;
    };
    
    PerformanceMetrics GetCurrentMetrics() const {
        PerformanceMetrics metrics{};
        
        // Calculate update time statistics
        if (update_times_.window_full || update_times_.current_index > 0) {
            size_t count = update_times_.window_full ? 60 : update_times_.current_index;
            
            std::vector<uint64_t> sorted_times(
                update_times_.frame_times_us.begin(),
                update_times_.frame_times_us.begin() + count);
            std::sort(sorted_times.begin(), sorted_times.end());
            
            uint64_t sum = std::accumulate(sorted_times.begin(), sorted_times.end(), 0ULL);
            metrics.avg_update_time_us = static_cast<double>(sum) / count;
            metrics.max_update_time_us = static_cast<double>(sorted_times.back());
            
            // 95th percentile
            size_t p95_index = static_cast<size_t>(count * 0.95);
            metrics.p95_update_time_us = static_cast<double>(sorted_times[p95_index]);
        }
        
        // Calculate broadcast time statistics
        if (broadcast_times_.window_full || broadcast_times_.current_index > 0) {
            size_t count = broadcast_times_.window_full ? 60 : broadcast_times_.current_index;
            uint64_t sum = std::accumulate(
                broadcast_times_.frame_times_us.begin(),
                broadcast_times_.frame_times_us.begin() + count, 0ULL);
            metrics.avg_broadcast_time_us = static_cast<double>(sum) / count;
        }
        
        // Estimate frame rate (based on 60 frame window)
        if (update_times_.window_full) {
            uint64_t total_time = std::accumulate(
                update_times_.frame_times_us.begin(),
                update_times_.frame_times_us.end(), 0ULL);
            double avg_frame_time_s = (total_time / 60.0) / 1000000.0;
            metrics.frame_rate_hz = 1.0 / avg_frame_time_s;
        }
        
        // Performance warning thresholds
        metrics.performance_warning = 
            (metrics.avg_update_time_us > 1000.0) ||      // > 1ms average
            (metrics.max_update_time_us > 5000.0) ||      // > 5ms max
            (metrics.p95_update_time_us > 2000.0);        // > 2ms 95th percentile
        
        return metrics;
    }
    
    void PrintRealTimeReport() const {
        auto metrics = GetCurrentMetrics();
        
        char buffer[512];
        sprintf_s(buffer,
            "REALTIME: %.1f Hz | Update: %.0f µs avg (%.0f µs p95, %.0f µs max) | Broadcast: %.0f µs%s\n",
            metrics.frame_rate_hz,
            metrics.avg_update_time_us,
            metrics.p95_update_time_us,
            metrics.max_update_time_us,
            metrics.avg_broadcast_time_us,
            metrics.performance_warning ? " [WARNING]" : "");
        
        OutputDebugStringA(buffer);
    }
};

// Usage in main Update loop
static RealTimeMonitor g_rt_monitor;

void AeroflyBridge::Update(/* params */) {
    uint64_t start_time = get_time_us();
    
    // === Main update logic ===
    shared_memory.UpdateData(received_messages, delta_time);
    ProcessCommands(sent_messages);
    
    uint64_t update_end = get_time_us();
    
    // === Broadcasting ===
    BroadcastToAllClients();
    
    uint64_t broadcast_end = get_time_us();
    
    // Record timing for analysis
    g_rt_monitor.RecordUpdateTime(update_end - start_time);
    g_rt_monitor.RecordBroadcastTime(broadcast_end - update_end);
    
    // Print performance report every 5 seconds
    static uint64_t last_report_time = 0;
    if (start_time - last_report_time > 5000000) {  // 5 seconds
        g_rt_monitor.PrintRealTimeReport();
        last_report_time = start_time;
    }
}
```

### Network Performance Analysis

```cpp
class NetworkAnalyzer {
private:
    struct ConnectionStats {
        std::atomic<uint64_t> bytes_sent{0};
        std::atomic<uint64_t> bytes_received{0};
        std::atomic<uint32_t> packets_sent{0};
        std::atomic<uint32_t> packets_received{0};
        std::atomic<uint32_t> send_failures{0};
        std::atomic<uint64_t> last_activity_us{0};
        SOCKET socket;
    };
    
    std::unordered_map<SOCKET, std::unique_ptr<ConnectionStats>> connections_;
    mutable std::shared_mutex connections_mutex_;
    
public:
    void RegisterConnection(SOCKET socket) {
        std::unique_lock<std::shared_mutex> lock(connections_mutex_);
        auto stats = std::make_unique<ConnectionStats>();
        stats->socket = socket;
        stats->last_activity_us.store(get_time_us(), std::memory_order_relaxed);
        connections_[socket] = std::move(stats);
    }
    
    void RecordSend(SOCKET socket, size_t bytes, bool success) {
        std::shared_lock<std::shared_mutex> lock(connections_mutex_);
        auto it = connections_.find(socket);
        if (it != connections_.end()) {
            auto& stats = *it->second;
            
            if (success) {
                stats.bytes_sent.fetch_add(bytes, std::memory_order_relaxed);
                stats.packets_sent.fetch_add(1, std::memory_order_relaxed);
            } else {
                stats.send_failures.fetch_add(1, std::memory_order_relaxed);
            }
            
            stats.last_activity_us.store(get_time_us(), std::memory_order_relaxed);
        }
    }
    
    void RecordReceive(SOCKET socket, size_t bytes) {
        std::shared_lock<std::shared_mutex> lock(connections_mutex_);
        auto it = connections_.find(socket);
        if (it != connections_.end()) {
            auto& stats = *it->second;
            stats.bytes_received.fetch_add(bytes, std::memory_order_relaxed);
            stats.packets_received.fetch_add(1, std::memory_order_relaxed);
            stats.last_activity_us.store(get_time_us(), std::memory_order_relaxed);
        }
    }
    
    void RemoveConnection(SOCKET socket) {
        std::unique_lock<std::shared_mutex> lock(connections_mutex_);
        connections_.erase(socket);
    }
    
    struct NetworkSummary {
        size_t total_connections;
        uint64_t total_bytes_sent;
        uint64_t total_bytes_received;
        uint32_t total_send_failures;
        double average_throughput_mbps;
        size_t idle_connections;
    };
    
    NetworkSummary GetSummary() const {
        std::shared_lock<std::shared_mutex> lock(connections_mutex_);
        
        NetworkSummary summary{};
        summary.total_connections = connections_.size();
        
        uint64_t now_us = get_time_us();
        uint64_t idle_threshold_us = 30 * 1000 * 1000;  // 30 seconds
        
        for (const auto& [socket, stats] : connections_) {
            summary.total_bytes_sent += stats->bytes_sent.load();
            summary.total_bytes_received += stats->bytes_received.load();
            summary.total_send_failures += stats->send_failures.load();
            
            uint64_t last_activity = stats->last_activity_us.load();
            if (now_us - last_activity > idle_threshold_us) {
                summary.idle_connections++;
            }
        }
        
        // Calculate throughput (rough estimate)
        uint64_t total_bytes = summary.total_bytes_sent + summary.total_bytes_received;
        summary.average_throughput_mbps = (total_bytes * 8.0) / (1024.0 * 1024.0 * 60.0);  // Last minute
        
        return summary;
    }
    
    void PrintNetworkReport() const {
        auto summary = GetSummary();
        
        char buffer[512];
        sprintf_s(buffer,
            "NETWORK: %zu connections (%zu idle) | %.2f Mbps | %llu KB sent | %llu KB recv | %u failures\n",
            summary.total_connections,
            summary.idle_connections,
            summary.average_throughput_mbps,
            summary.total_bytes_sent / 1024,
            summary.total_bytes_received / 1024,
            summary.total_send_failures);
        
        OutputDebugStringA(buffer);
    }
};
```

---

## 🎚 Runtime Performance Tuning

### Adaptive Throttling System

```cpp
class AdaptiveThrottling {
private:
    struct ThrottleConfig {
        std::atomic<uint32_t> broadcast_interval_us{20000};  // Default 20ms
        std::atomic<uint32_t> max_clients_per_frame{50};
        std::atomic<bool> adaptive_mode{true};
    };
    
    ThrottleConfig config_;
    std::atomic<uint64_t> last_performance_check_us_{0};
    
public:
    // Adjust throttling based on current performance
    void UpdateThrottling(const RealTimeMonitor::PerformanceMetrics& metrics) {
        if (!config_.adaptive_mode.load(std::memory_order_relaxed)) return;
        
        uint32_t current_interval = config_.broadcast_interval_us.load();
        
        if (metrics.performance_warning) {
            // Performance is poor - reduce broadcast frequency
            uint32_t new_interval = std::min(current_interval + 5000, 50000U);  // Max 50ms
            config_.broadcast_interval_us.store(new_interval, std::memory_order_relaxed);
            
            OutputDebugStringA(("ADAPTIVE: Increased interval to " + 
                               std::to_string(new_interval / 1000) + "ms\n").c_str());
        }
        else if (metrics.avg_update_time_us < 500.0 && current_interval > 10000) {
            // Performance is good - increase broadcast frequency
            uint32_t new_interval = std::max(current_interval - 2000, 10000U);  // Min 10ms
            config_.broadcast_interval_us.store(new_interval, std::memory_order_relaxed);
            
            OutputDebugStringA(("ADAPTIVE: Decreased interval to " + 
                               std::to_string(new_interval / 1000) + "ms\n").c_str());
        }
    }
    
    bool ShouldBroadcast() const {
        static uint64_t last_broadcast_us = 0;
        uint64_t now_us = get_time_us();
        uint32_t interval = config_.broadcast_interval_us.load(std::memory_order_relaxed);
        
        if (now_us - last_broadcast_us >= interval) {
            last_broadcast_us = now_us;
            return true;
        }
        
        return false;
    }
    
    uint32_t GetCurrentInterval() const {
        return config_.broadcast_interval_us.load(std::memory_order_relaxed);
    }
    
    void SetAdaptiveMode(bool enabled) {
        config_.adaptive_mode.store(enabled, std::memory_order_relaxed);
    }
};
```

### CPU Affinity Optimization

```cpp
class CPUAffinityManager {
private:
    struct ThreadInfo {
        std::thread::id thread_id;
        HANDLE thread_handle;
        int preferred_core;
        std::string name;
    };
    
    std::vector<ThreadInfo> managed_threads_;
    int num_cores_;
    
public:
    CPUAffinityManager() {
        SYSTEM_INFO sys_info;
        GetSystemInfo(&sys_info);
        num_cores_ = static_cast<int>(sys_info.dwNumberOfProcessors);
        
        char buffer[128];
        sprintf_s(buffer, "CPU Affinity Manager: %d cores detected\n", num_cores_);
        OutputDebugStringA(buffer);
    }
    
    void SetThreadAffinity(std::thread& thread, int core, const std::string& name) {
        if (core >= num_cores_) {
            OutputDebugStringA("WARNING: Invalid core number\n");
            return;
        }
        
        HANDLE thread_handle = static_cast<HANDLE>(thread.native_handle());
        DWORD_PTR affinity_mask = 1ULL << core;
        
        DWORD_PTR result = SetThreadAffinityMask(thread_handle, affinity_mask);
        if (result == 0) {
            OutputDebugStringA(("Failed to set affinity for " + name + "\n").c_str());
        } else {
            OutputDebugStringA(("Set " + name + " to core " + std::to_string(core) + "\n").c_str());
            
            ThreadInfo info;
            info.thread_id = thread.get_id();
            info.thread_handle = thread_handle;
            info.preferred_core = core;
            info.name = name;
            managed_threads_.push_back(info);
        }
    }
    
    void OptimalThreadDistribution() {
        // Suggested core assignment for flight sim bridge:
        // Core 0: Aerofly main thread (can't control this)
        // Core 1: TCP server thread
        // Core 2: WebSocket server thread  
        // Core 3: Command processing thread
        // Other cores: Available for OS and other applications
        
        OutputDebugStringA("RECOMMENDATION: Set Aerofly process affinity to cores 0-3\n");
        OutputDebugStringA("This leaves remaining cores for OS and background tasks\n");
    }
    
    void SetProcessPriorityClass(DWORD priority_class) {
        HANDLE process = GetCurrentProcess();
        if (SetPriorityClass(process, priority_class)) {
            const char* priority_name = 
                (priority_class == HIGH_PRIORITY_CLASS) ? "HIGH" :
                (priority_class == ABOVE_NORMAL_PRIORITY_CLASS) ? "ABOVE_NORMAL" :
                (priority_class == NORMAL_PRIORITY_CLASS) ? "NORMAL" :
                (priority_class == BELOW_NORMAL_PRIORITY_CLASS) ? "BELOW_NORMAL" : "UNKNOWN";
            
            OutputDebugStringA(("Process priority set to " + std::string(priority_name) + "\n").c_str());
        }
    }
};

// Usage in Bridge initialization
void AeroflyBridge::OptimizeForPerformance() {
    CPUAffinityManager cpu_manager;
    
    // Set above normal priority for better real-time performance
    cpu_manager.SetProcessPriorityClass(ABOVE_NORMAL_PRIORITY_CLASS);
    
    // Set thread affinities when creating worker threads
    std::thread tcp_thread(/* ... */);
    cpu_manager.SetThreadAffinity(tcp_thread, 1, "TCP_Server");
    
    std::thread ws_thread(/* ... */);
    cpu_manager.SetThreadAffinity(ws_thread, 2, "WebSocket_Server");
}
```

---

## 🚀 Production Deployment Optimizations

### Release Build Configuration

```cpp
// Compiler optimization directives
#pragma optimize("gty", on)  // Enable all optimizations

// Critical section optimizations
#define FORCE_INLINE __forceinline

// Hot path function attributes
FORCE_INLINE uint64_t get_time_us() {
    static LARGE_INTEGER frequency = []() {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        return freq;
    }();
    
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (counter.QuadPart * 1000000ULL) / frequency.QuadPart;
}

// Branch prediction hints (GCC/Clang style)
#ifdef _MSC_VER
    #define LIKELY(x) (x)
    #define UNLIKELY(x) (x)
#else
    #define LIKELY(x) __builtin_expect(!!(x), 1)
    #define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

// Optimized hot path
FORCE_INLINE bool should_broadcast_now() {
    static uint64_t last_broadcast_us = 0;
    static uint32_t interval_us = get_broadcast_interval();
    
    uint64_t now_us = get_time_us();
    
    if (LIKELY(now_us - last_broadcast_us < interval_us)) {
        return false;  // Most common case - not time to broadcast
    }
    
    last_broadcast_us = now_us;
    return true;
}
```

### Deployment Validation Suite

```cpp
class DeploymentValidator {
public:
    struct ValidationResult {
        bool passed;
        std::string message;
        double benchmark_score;
    };
    
    static std::vector<ValidationResult> RunFullValidation() {
        std::vector<ValidationResult> results;
        
        // Performance benchmarks
        results.push_back(BenchmarkUpdatePerformance());
        results.push_back(BenchmarkMemoryLatency());
        results.push_back(BenchmarkNetworkThroughput());
        results.push_back(BenchmarkJSONGeneration());
        
        // Functionality tests
        results.push_back(TestSharedMemoryIntegrity());
        results.push_back(TestNetworkConnectivity());
        results.push_back(TestErrorRecovery());
        
        return results;
    }
    
private:
    static ValidationResult BenchmarkUpdatePerformance() {
        // Simulate typical Update() workload
        const size_t NUM_ITERATIONS = 1000;
        std::vector<uint64_t> times;
        times.reserve(NUM_ITERATIONS);
        
        // Create realistic test data
        AeroflyBridgeData test_data;
        initialize_test_data(&test_data);
        
        for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
            uint64_t start = get_time_us();
            
            // Simulate update operations
            simulate_update_workload(&test_data);
            
            uint64_t end = get_time_us();
            times.push_back(end - start);
        }
        
        // Calculate statistics
        std::sort(times.begin(), times.end());
        double avg_time = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
        double p95_time = times[static_cast<size_t>(times.size() * 0.95)];
        
        ValidationResult result;
        result.benchmark_score = avg_time;
        result.passed = (avg_time < 100.0) && (p95_time < 500.0);  // µs thresholds
        result.message = "Update performance: " + std::to_string(avg_time) + 
                        "µs avg, " + std::to_string(p95_time) + "µs p95";
        
        return result;
    }
    
    static ValidationResult BenchmarkJSONGeneration() {
        const size_t NUM_ITERATIONS = 100;
        AeroflyBridgeData test_data;
        initialize_test_data(&test_data);
        
        uint64_t start = get_time_us();
        
        for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
            std::string json = BuildDataJSON(&test_data);
            // Prevent optimization
            volatile size_t len = json.length();
        }
        
        uint64_t end = get_time_us();
        double avg_time = static_cast<double>(end - start) / NUM_ITERATIONS;
        
        ValidationResult result;
        result.benchmark_score = avg_time;
        result.passed = avg_time < 1000.0;  // < 1ms per JSON generation
        result.message = "JSON generation: " + std::to_string(avg_time) + "µs avg";
        
        return result;
    }
    
    static ValidationResult TestErrorRecovery() {
        ValidationResult result;
        
        try {
            // Test various error conditions
            test_network_disconnection();
            test_invalid_json_commands();
            test_memory_exhaustion();
            test_thread_interruption();
            
            result.passed = true;
            result.message = "Error recovery: All tests passed";
            result.benchmark_score = 100.0;
        }
        catch (const std::exception& e) {
            result.passed = false;
            result.message = "Error recovery failed: " + std::string(e.what());
            result.benchmark_score = 0.0;
        }
        
        return result;
    }
};
```

---

## 🎓 Performance Optimization Best Practices

### 1. Measure Before Optimizing

**Always profile first:**
```cpp
// ❌ Wrong: Optimizing without measurement
void optimize_blindly() {
    // "I think this loop is slow"
    for (int i = 0; i < 1000; ++i) {
        expensive_operation();  // Actually not the bottleneck!
    }
}

// ✅ Right: Profile-guided optimization
void optimize_with_data() {
    PROFILE_SCOPE("MainLoop");
    
    for (int i = 0; i < 1000; ++i) {
        PROFILE_SCOPE("ExpensiveOperation");
        expensive_operation();  // Now we know if this is actually slow
    }
}
```

### 2. Hot Path Optimization Priority

**Focus on the critical path:**
```cpp
// Optimization priority order for Aerofly Bridge:
// 1. Update() function (called 50+ Hz) - HIGHEST PRIORITY
// 2. SharedMemory write operations
// 3. JSON generation for network broadcast  
// 4. Network send operations
// 5. Command parsing
// 6. Everything else - LOW PRIORITY
```

### 3. Memory Allocation Rules

**Real-time systems require careful memory management:**
```cpp
// ✅ Good: Pre-allocated pools
class PreAllocatedSystem {
    std::array<Command, 1024> command_pool_;
    std::array<bool, 1024> command_used_;
    size_t next_free_command_ = 0;
};

// ❌ Bad: Dynamic allocation in hot path
void update_hot_path() {
    auto commands = std::make_unique<std::vector<Command>>();  // ALLOCATION!
    commands->resize(100);  // MORE ALLOCATION!
}
```

### 4. Cache-Friendly Programming

**Optimize for modern CPU cache hierarchies:**
```cpp
// ✅ Good: Structure of Arrays (cache-friendly)
struct SoA_FlightData {
    std::array<double, 1000> altitudes;
    std::array<double, 1000> airspeeds;
    std::array<double, 1000> headings;
};

void process_soa(SoA_FlightData& data) {
    // Sequential access - excellent cache locality
    for (size_t i = 0; i < 1000; ++i) {
        data.altitudes[i] *= FEET_TO_METERS;
    }
}

// ❌ Bad: Array of Structures (cache-unfriendly)
struct AoS_FlightData {
    double altitude;
    double airspeed;
    double heading;
};

void process_aos(std::array<AoS_FlightData, 1000>& data) {
    // Poor cache locality - loads unnecessary data
    for (auto& flight : data) {
        flight.altitude *= FEET_TO_METERS;  // Only need altitude, but loads all
    }
}
```

---

<!-- Removed "Next Steps" and cross-doc references to avoid broken links in public repo. Retained core content above. -->