# Thread-Safe Programming

**Mastering synchronization in real-time flight simulation systems**

Copyright (c) 2025 Juan Luis Gabriel

This tutorial explores the thread-safety techniques used in the Aerofly Bridge, teaching you how to write robust multi-threaded applications for flight simulation. Learn to handle race conditions, implement lock-free algorithms, and design high-performance concurrent systems.

## 🎯 What You'll Learn

- **Race condition** identification and prevention
- **Synchronization primitives** (mutex, atomic, locks)
- **Lock-free programming** patterns
- **Producer-Consumer** architectures
- **Memory ordering** and visibility
- **Performance vs safety** trade-offs
- **Real-time constraints** in threading

## 📋 Prerequisites

- **[DLL Development Basics](01_dll_basics.md)** completed
- **[Architecture Deep Dive](02_architecture_explained.md)** understood
- **C++ threading** (std::thread, std::mutex basics)
- **Memory model** awareness (helpful but not required)

---

## ⚠️ Why Thread Safety Matters in Flight Sim

### The Real-Time Challenge

Flight simulation DLLs operate under **strict timing constraints**:

```cpp
/*
Timing Requirements:
┌──────────────────┬─────────────┬──────────────────┐
│ Operation        │ Max Time    │ Consequence      │
├──────────────────┼─────────────┼──────────────────┤
│ Update()         │ ~1ms        │ Sim stuttering  │
│ Data broadcast   │ ~5ms        │ Client lag       │
│ Command process  │ ~10ms       │ Control delay    │
│ Memory access    │ ~1µs        │ Cache miss cost  │
└──────────────────┴─────────────┴──────────────────┘
*/
```

**Common threading problems in flight sim:**
- **Data corruption** → Incorrect instrument readings
- **Race conditions** → Inconsistent aircraft state  
- **Deadlocks** → Complete simulation freeze
- **Priority inversion** → Control lag during critical phases

### Multi-Threading in the Bridge

The Aerofly Bridge uses **4 concurrent threads** that must coordinate safely:

```cpp
/*
Thread Interaction Map:
                    ┌─────────────────┐
                    │ Aerofly Thread  │ ← 50-60 Hz (CRITICAL)
                    │ (Update calls)  │
                    └─────────┬───────┘
                              │ Writes
                              ▼
                    ┌─────────────────┐
                    │ Shared Memory   │ ← Single writer, multiple readers
                    │ (AeroflyData)   │
                    └─────────┬───────┘
                              │ Reads
        ┌─────────────────────┼─────────────────────┐
        ▼                     ▼                     ▼
┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│ TCP Thread  │    │ Command     │    │ WebSocket   │
│ (Broadcast) │    │ Thread      │    │ Thread      │
└─────────────┘    └─────────────┘    └─────────────┘
      │                    │                    │
      └─── Command Queues ─┴─── Thread-safe ───┘
*/
```

---

## 🔒 Synchronization Primitives Deep Dive

### 1. Mutex (Mutual Exclusion)

**Basic mutex usage** for protecting critical sections:

```cpp
class SafeCounter {
private:
    mutable std::mutex mutex_;
    int counter_ = 0;
    
public:
    void increment() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++counter_;  // Protected operation
    }
    
    int get() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return counter_;  // Protected read
    }
    
    // RAII ensures automatic unlock even with exceptions
};
```

**Real Bridge example - Client list protection:**
```cpp
class TCPServerInterface {
private:
    mutable std::mutex clients_mutex;
    std::vector<SOCKET> client_sockets;
    
public:
    void AddClient(SOCKET client) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        client_sockets.push_back(client);
        // Automatic unlock when lock goes out of scope
    }
    
    void RemoveClient(SOCKET client) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = std::find(client_sockets.begin(), client_sockets.end(), client);
        if (it != client_sockets.end()) {
            client_sockets.erase(it);
        }
    }
    
    std::vector<SOCKET> GetClientSnapshot() const {
        std::lock_guard<std::mutex> lock(clients_mutex);
        return client_sockets;  // Copy for thread-safe iteration
    }
};
```

### 2. Atomic Operations

**Lock-free synchronization** for simple operations:

```cpp
class PerformanceMonitor {
private:
    std::atomic<uint64_t> update_count_{0};
    std::atomic<uint64_t> total_update_time_us_{0};
    std::atomic<bool> running_{false};
    
public:
    void RecordUpdate(uint64_t duration_us) {
        // These operations are atomic (lock-free)
        update_count_.fetch_add(1, std::memory_order_relaxed);
        total_update_time_us_.fetch_add(duration_us, std::memory_order_relaxed);
    }
    
    void Start() {
        running_.store(true, std::memory_order_release);
    }
    
    void Stop() {
        running_.store(false, std::memory_order_release);
    }
    
    bool IsRunning() const {
        return running_.load(std::memory_order_acquire);
    }
    
    double GetAverageUpdateTime() const {
        uint64_t count = update_count_.load(std::memory_order_relaxed);
        uint64_t total = total_update_time_us_.load(std::memory_order_relaxed);
        return count > 0 ? static_cast<double>(total) / count : 0.0;
    }
};
```

### 3. Condition Variables

**Thread coordination** for producer-consumer patterns:

```cpp
class CommandQueue {
private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<std::string> commands_;
    bool shutdown_ = false;
    
public:
    void Push(const std::string& command) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            commands_.push(command);
        }
        condition_.notify_one();  // Wake up waiting consumer
    }
    
    bool WaitAndPop(std::string& command, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // Wait until: queue not empty OR shutdown OR timeout
        bool success = condition_.wait_for(lock, timeout, [this] {
            return !commands_.empty() || shutdown_;
        });
        
        if (!success || shutdown_) {
            return false;  // Timeout or shutdown
        }
        
        command = commands_.front();
        commands_.pop();
        return true;
    }
    
    void Shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_ = true;
        }
        condition_.notify_all();  // Wake up all waiting threads
    }
};
```

---

## 🚀 Lock-Free Programming Patterns

### Single-Producer, Single-Consumer (SPSC) Queue

**Ultra-high performance** for specific use cases:

```cpp
template<typename T, size_t Size>
class SPSCQueue {
private:
    std::array<T, Size> buffer_;
    std::atomic<size_t> write_index_{0};
    std::atomic<size_t> read_index_{0};
    
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");
    static constexpr size_t INDEX_MASK = Size - 1;
    
public:
    // Producer thread only
    bool try_push(T&& item) {
        const size_t current_write = write_index_.load(std::memory_order_relaxed);
        const size_t next_write = (current_write + 1) & INDEX_MASK;
        
        if (next_write == read_index_.load(std::memory_order_acquire)) {
            return false;  // Queue full
        }
        
        buffer_[current_write] = std::move(item);
        write_index_.store(next_write, std::memory_order_release);
        return true;
    }
    
    // Consumer thread only
    bool try_pop(T& item) {
        const size_t current_read = read_index_.load(std::memory_order_relaxed);
        
        if (current_read == write_index_.load(std::memory_order_acquire)) {
            return false;  // Queue empty
        }
        
        item = std::move(buffer_[current_read]);
        read_index_.store((current_read + 1) & INDEX_MASK, std::memory_order_release);
        return true;
    }
    
    size_t size() const {
        const size_t write = write_index_.load(std::memory_order_acquire);
        const size_t read = read_index_.load(std::memory_order_acquire);
        return (write - read) & INDEX_MASK;
    }
};

// Usage in Bridge
SPSCQueue<std::string, 1024> command_queue;

// Producer (Command thread)
void CommandThread::ProcessIncoming() {
    std::string command = receive_from_client();
    if (!command_queue.try_push(std::move(command))) {
        // Queue full - handle overflow
        OutputDebugStringA("Command queue overflow!\n");
    }
}

// Consumer (Aerofly thread)
void AeroflyBridge::Update(/* ... */) {
    std::string command;
    while (command_queue.try_pop(command)) {
        process_command(command);
    }
}
```

### Lock-Free Reference Counting

**Safe memory management** without locks:

```cpp
template<typename T>
class LockFreeSharedPtr {
private:
    struct ControlBlock {
        std::atomic<int> ref_count{1};
        T* ptr;
        
        ControlBlock(T* p) : ptr(p) {}
    };
    
    ControlBlock* control_;
    
public:
    explicit LockFreeSharedPtr(T* ptr) : control_(new ControlBlock(ptr)) {}
    
    LockFreeSharedPtr(const LockFreeSharedPtr& other) : control_(other.control_) {
        if (control_) {
            control_->ref_count.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    ~LockFreeSharedPtr() {
        if (control_ && control_->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete control_->ptr;
            delete control_;
        }
    }
    
    T* get() const { return control_ ? control_->ptr : nullptr; }
    T& operator*() const { return *get(); }
    T* operator->() const { return get(); }
};

// Usage for shared configuration
LockFreeSharedPtr<Configuration> g_config;

void UpdateThread() {
    auto config = g_config;  // Atomic increment
    use_config(*config);     // Safe to use
}  // Automatic decrement
```

---

## 🔄 Producer-Consumer Patterns in the Bridge

### Multi-Producer, Single-Consumer (Commands)

**Multiple network threads** → **Single Aerofly thread**:

```cpp
class ThreadSafeCommandProcessor {
private:
    struct Command {
        std::string json;
        uint64_t timestamp_us;
        int source_thread_id;
    };
    
    mutable std::mutex command_mutex_;
    std::queue<Command> pending_commands_;
    std::atomic<size_t> total_commands_{0};
    
public:
    // Called by multiple network threads
    void EnqueueCommand(const std::string& json, int source_id) {
        Command cmd{json, get_time_us(), source_id};
        
        {
            std::lock_guard<std::mutex> lock(command_mutex_);
            pending_commands_.push(std::move(cmd));
        }
        
        total_commands_.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Called only by Aerofly thread
    std::vector<tm_external_message> ProcessPendingCommands() {
        std::vector<Command> commands_to_process;
        
        // Quickly extract all pending commands
        {
            std::lock_guard<std::mutex> lock(command_mutex_);
            
            while (!pending_commands_.empty()) {
                commands_to_process.push_back(std::move(pending_commands_.front()));
                pending_commands_.pop();
            }
        }
        
        // Process outside the lock (slow operations)
        std::vector<tm_external_message> messages;
        for (const auto& cmd : commands_to_process) {
            auto msg = ParseJSONCommand(cmd.json);
            if (msg.GetDataType() != tm_msg_data_type::None) {
                messages.push_back(msg);
            }
        }
        
        return messages;
    }
    
    size_t GetTotalCommandsProcessed() const {
        return total_commands_.load(std::memory_order_relaxed);
    }
};
```

### Single-Producer, Multi-Consumer (Data Broadcasting)

**Single Aerofly thread** → **Multiple network threads**:

```cpp
class ThreadSafeBroadcaster {
private:
    struct BroadcastData {
        std::string json;
        uint64_t timestamp_us;
        uint32_t sequence_number;
    };
    
    mutable std::shared_mutex data_mutex_;
    std::shared_ptr<BroadcastData> latest_data_;
    std::atomic<uint32_t> sequence_counter_{0};
    
public:
    // Called only by Aerofly thread (producer)
    void UpdateData(const AeroflyBridgeData* data) {
        std::string json = BuildDataJSON(data);
        uint32_t seq = sequence_counter_.fetch_add(1, std::memory_order_relaxed);
        
        auto new_data = std::make_shared<BroadcastData>(
            BroadcastData{std::move(json), get_time_us(), seq}
        );
        
        {
            std::unique_lock<std::shared_mutex> lock(data_mutex_);
            latest_data_ = new_data;
        }
    }
    
    // Called by multiple network threads (consumers)
    std::shared_ptr<BroadcastData> GetLatestData() const {
        std::shared_lock<std::shared_mutex> lock(data_mutex_);
        return latest_data_;  // Shared ownership
    }
    
    // Network thread usage
    void NetworkThread::BroadcastToClients() {
        auto data = broadcaster_.GetLatestData();
        if (data && data->sequence_number > last_sent_sequence_) {
            send_to_all_clients(data->json);
            last_sent_sequence_ = data->sequence_number;
        }
    }
};
```

---

## ⚡ Real-Time Memory Access Patterns

### The Shared Memory Challenge

**Problem**: Aerofly thread writes, multiple readers access simultaneously

```cpp
/*
Challenge:
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│ Aerofly Thread  │    │ TCP Thread      │    │ Local App       │
│ (Writer)        │    │ (Reader)        │    │ (Reader)        │
├─────────────────┤    ├─────────────────┤    ├─────────────────┤
│ altitude = 1000 │───▶│ Read altitude   │───▶│ Read altitude   │
│ altitude = 1001 │    │ Read airspeed   │    │ Read airspeed   │
│ airspeed = 120  │    │ Build JSON      │    │ Update display  │
│ airspeed = 121  │    │ Send to client  │    │                 │
└─────────────────┘    └─────────────────┘    └─────────────────┘
       ▲                        ▲                        ▲
       │ Problem: What if readers see inconsistent state? │
       └─────────────────────────────────────────────────┘
*/
```

**Solution**: Double-buffering with atomic flags

```cpp
class AtomicDataBuffer {
private:
    struct BufferPair {
        AeroflyBridgeData buffer1;
        AeroflyBridgeData buffer2;
        std::atomic<int> active_buffer{0};  // 0 or 1
        std::atomic<bool> buffer1_ready{false};
        std::atomic<bool> buffer2_ready{false};
    };
    
    BufferPair buffers_;
    
public:
    // Writer thread only
    void UpdateData(const std::vector<tm_external_message>& messages) {
        int current_active = buffers_.active_buffer.load(std::memory_order_acquire);
        int write_buffer = 1 - current_active;  // Write to inactive buffer
        
        AeroflyBridgeData* write_ptr = (write_buffer == 0) ? 
            &buffers_.buffer1 : &buffers_.buffer2;
        
        // Update the inactive buffer
        write_ptr->data_valid = 0;  // Mark as updating
        process_messages(write_ptr, messages);
        write_ptr->timestamp_us = get_time_us();
        write_ptr->data_valid = 1;  // Mark as ready
        
        // Atomically switch active buffer
        buffers_.active_buffer.store(write_buffer, std::memory_order_release);
        
        // Mark new buffer as ready
        if (write_buffer == 0) {
            buffers_.buffer1_ready.store(true, std::memory_order_release);
        } else {
            buffers_.buffer2_ready.store(true, std::memory_order_release);
        }
    }
    
    // Reader threads
    const AeroflyBridgeData* GetReadData() const {
        int active = buffers_.active_buffer.load(std::memory_order_acquire);
        
        const AeroflyBridgeData* read_ptr = (active == 0) ? 
            &buffers_.buffer1 : &buffers_.buffer2;
        
        // Additional safety check
        if (read_ptr->data_valid == 1) {
            return read_ptr;
        }
        
        return nullptr;  // Data not ready
    }
};
```

### Copy-Free String Sharing

**Problem**: JSON generation is expensive, avoid copying

```cpp
class ThreadSafeStringCache {
private:
    struct CachedString {
        std::string data;
        std::atomic<int> ref_count{0};
        uint64_t timestamp_us;
    };
    
    mutable std::mutex cache_mutex_;
    std::shared_ptr<CachedString> current_string_;
    
public:
    // Producer: Update with new string
    void UpdateString(std::string new_data) {
        auto new_cached = std::make_shared<CachedString>();
        new_cached->data = std::move(new_data);
        new_cached->timestamp_us = get_time_us();
        
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            current_string_ = new_cached;
        }
    }
    
    // Consumers: Get shared reference
    std::shared_ptr<CachedString> GetString() const {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        return current_string_;  // Shared ownership, no copy
    }
};

// Usage pattern
ThreadSafeStringCache json_cache;

// Producer (Aerofly thread)
void UpdateJSON(const AeroflyBridgeData* data) {
    std::string json = BuildDataJSON(data);  // Expensive operation
    json_cache.UpdateString(std::move(json));  // Share with consumers
}

// Consumers (Network threads)
void BroadcastToClients() {
    auto cached_json = json_cache.GetString();
    if (cached_json) {
        send_to_all_clients(cached_json->data);  // No copy needed
    }
}
```

---

## 🔍 Memory Ordering and Visibility

### Understanding Memory Ordering

**Different guarantees** for different use cases:

```cpp
class MemoryOrderingExamples {
private:
    std::atomic<bool> ready_{false};
    std::atomic<int> data_{0};
    std::atomic<int> counter_{0};
    
public:
    // Relaxed ordering: No synchronization, only atomicity
    void RelaxedIncrement() {
        counter_.fetch_add(1, std::memory_order_relaxed);
        // Fastest, but no ordering guarantees with other operations
    }
    
    // Acquire-Release: Synchronizes with matching operations
    void ProducerThread() {
        data_.store(42, std::memory_order_relaxed);     // Write data first
        ready_.store(true, std::memory_order_release);  // Signal ready
        // All writes above are visible to acquire operations
    }
    
    void ConsumerThread() {
        while (!ready_.load(std::memory_order_acquire)) {  // Wait for signal
            std::this_thread::yield();
        }
        // All writes from producer are now visible
        int value = data_.load(std::memory_order_relaxed);  // Guaranteed to see 42
    }
    
    // Sequential consistency: Strongest guarantee (default)
    void SequentialOperations() {
        data_.store(100);   // memory_order_seq_cst (default)
        ready_.store(true); // memory_order_seq_cst (default)
        // Global ordering with all other seq_cst operations
    }
};
```

### Bridge-Specific Memory Ordering

**Optimized for flight sim requirements:**

```cpp
class FlightDataBuffer {
private:
    struct FlightData {
        std::atomic<uint64_t> timestamp_us;
        std::atomic<uint32_t> sequence_number;
        double altitude;           // Not atomic - updated under lock
        double airspeed;          // Not atomic - updated under lock
        double heading;           // Not atomic - updated under lock
        std::atomic<bool> valid;  // Signals completion
    };
    
    FlightData data_;
    mutable std::mutex update_mutex_;
    
public:
    // Writer: Aerofly thread
    void UpdateFlightData(double alt, double ias, double hdg) {
        {
            std::lock_guard<std::mutex> lock(update_mutex_);
            
            // Mark as invalid during update
            data_.valid.store(false, std::memory_order_relaxed);
            
            // Update non-atomic fields (protected by mutex)
            data_.altitude = alt;
            data_.airspeed = ias;
            data_.heading = hdg;
            
            // Update atomic fields
            data_.timestamp_us.store(get_time_us(), std::memory_order_relaxed);
            data_.sequence_number.fetch_add(1, std::memory_order_relaxed);
        }
        
        // Signal completion (release ensures all writes are visible)
        data_.valid.store(true, std::memory_order_release);
    }
    
    // Readers: Network threads (lock-free)
    bool ReadFlightData(double& alt, double& ias, double& hdg, uint64_t& timestamp) {
        // Check validity (acquire ensures we see all writes)
        if (!data_.valid.load(std::memory_order_acquire)) {
            return false;  // Data currently being updated
        }
        
        // Read atomic fields
        timestamp = data_.timestamp_us.load(std::memory_order_relaxed);
        
        // Read non-atomic fields (safe because valid == true)
        alt = data_.altitude;
        ias = data_.airspeed;
        hdg = data_.heading;
        
        // Verify data is still valid
        return data_.valid.load(std::memory_order_acquire);
    }
};
```

---

## 🚨 Common Threading Pitfalls and Solutions

### Pitfall 1: ABA Problem

**Problem**: Value changes and changes back

```cpp
// ❌ Dangerous: ABA problem
class UnsafeStack {
private:
    std::atomic<Node*> head_{nullptr};
    
public:
    void push(T item) {
        Node* new_node = new Node{item, head_.load()};
        
        // Problem: head_ might change between load and compare_exchange
        while (!head_.compare_exchange_weak(new_node->next, new_node)) {
            // ABA: head_ might have same value but different history
        }
    }
};

// ✅ Solution: Tagged pointers or hazard pointers
class SafeStack {
private:
    struct TaggedPointer {
        Node* ptr;
        uintptr_t tag;
    };
    
    std::atomic<TaggedPointer> head_{{nullptr, 0}};
    
public:
    void push(T item) {
        Node* new_node = new Node{item};
        TaggedPointer current_head = head_.load();
        
        do {
            new_node->next = current_head.ptr;
            TaggedPointer new_head{new_node, current_head.tag + 1};
        } while (!head_.compare_exchange_weak(current_head, new_head));
    }
};
```

### Pitfall 2: Priority Inversion

**Problem**: High priority thread blocked by low priority thread

```cpp
// ❌ Problem scenario
void HighPriorityThread() {
    std::lock_guard<std::mutex> lock(shared_mutex);
    // Blocked if low priority thread holds the lock
    critical_flight_control_update();
}

void LowPriorityThread() {
    std::lock_guard<std::mutex> lock(shared_mutex);
    slow_logging_operation();  // Blocks high priority thread
}

// ✅ Solution: Use try_lock with timeout in high priority threads
void HighPriorityThread() {
    std::unique_lock<std::mutex> lock(shared_mutex, std::defer_lock);
    
    if (lock.try_lock_for(std::chrono::microseconds(100))) {
        critical_flight_control_update();
    } else {
        // Fallback: use cached data or skip this update
        OutputDebugStringA("WARNING: High priority operation delayed\n");
        use_cached_flight_data();
    }
}
```

### Pitfall 3: False Sharing

**Problem**: Cache line bouncing between cores

```cpp
// ❌ False sharing problem
struct BadCounters {
    std::atomic<int> tcp_count;     // Same cache line
    std::atomic<int> websocket_count;  // Same cache line  
    std::atomic<int> shared_mem_count; // Same cache line
};

// Each thread updates different counters but they share cache lines
// Result: Cache line bouncing, poor performance

// ✅ Solution: Align to cache lines
struct alignas(64) GoodCounters {  // 64 bytes = typical cache line
    std::atomic<int> tcp_count;
    char padding1[60];  // Force separate cache lines
    
    std::atomic<int> websocket_count;
    char padding2[60];
    
    std::atomic<int> shared_mem_count;
    char padding3[60];
};

// Or use thread-local storage
thread_local int tcp_operations = 0;
thread_local int websocket_operations = 0;

void TCPThread() {
    ++tcp_operations;  // No contention
    
    // Periodically sync to global counter
    if (tcp_operations % 100 == 0) {
        global_tcp_counter.fetch_add(100, std::memory_order_relaxed);
        tcp_operations = 0;
    }
}
```

---

## 📊 Performance Monitoring and Debugging

### Thread-Safe Performance Monitoring

```cpp
class ThreadSafeProfiler {
private:
    struct ProfileData {
        std::atomic<uint64_t> call_count{0};
        std::atomic<uint64_t> total_time_us{0};
        std::atomic<uint64_t> max_time_us{0};
        std::atomic<uint64_t> min_time_us{UINT64_MAX};
    };
    
    std::unordered_map<std::string, std::unique_ptr<ProfileData>> profiles_;
    mutable std::shared_mutex profiles_mutex_;
    
public:
    class ScopedTimer {
    private:
        ProfileData* data_;
        uint64_t start_time_;
        
    public:
        ScopedTimer(ProfileData* data) : data_(data), start_time_(get_time_us()) {}
        
        ~ScopedTimer() {
            uint64_t duration = get_time_us() - start_time_;
            
            data_->call_count.fetch_add(1, std::memory_order_relaxed);
            data_->total_time_us.fetch_add(duration, std::memory_order_relaxed);
            
            // Update max (compare-and-swap loop)
            uint64_t current_max = data_->max_time_us.load(std::memory_order_relaxed);
            while (duration > current_max && 
                   !data_->max_time_us.compare_exchange_weak(current_max, duration)) {
            }
            
            // Update min
            uint64_t current_min = data_->min_time_us.load(std::memory_order_relaxed);
            while (duration < current_min && 
                   !data_->min_time_us.compare_exchange_weak(current_min, duration)) {
            }
        }
    };
    
    ScopedTimer Profile(const std::string& name) {
        // Fast path: try shared lock first
        {
            std::shared_lock<std::shared_mutex> lock(profiles_mutex_);
            auto it = profiles_.find(name);
            if (it != profiles_.end()) {
                return ScopedTimer(it->second.get());
            }
        }
        
        // Slow path: create new profile entry
        {
            std::unique_lock<std::shared_mutex> lock(profiles_mutex_);
            auto [it, inserted] = profiles_.emplace(name, std::make_unique<ProfileData>());
            return ScopedTimer(it->second.get());
        }
    }
    
    void PrintStats() const {
        std::shared_lock<std::shared_mutex> lock(profiles_mutex_);
        
        for (const auto& [name, data] : profiles_) {
            uint64_t calls = data->call_count.load();
            uint64_t total = data->total_time_us.load();
            uint64_t max_time = data->max_time_us.load();
            uint64_t min_time = data->min_time_us.load();
            
            if (calls > 0) {
                OutputDebugStringA((name + ": " + 
                    std::to_string(calls) + " calls, " +
                    std::to_string(total / calls) + "µs avg, " +
                    std::to_string(max_time) + "µs max, " +
                    std::to_string(min_time) + "µs min\n").c_str());
            }
        }
    }
};

// Usage
ThreadSafeProfiler g_profiler;

void AeroflyBridge::Update(/* ... */) {
    auto timer = g_profiler.Profile("AeroflyBridge::Update");
    
    // Your update logic here
    shared_memory.UpdateData(received_messages, delta_time);
    
    // Timer automatically records duration when it goes out of scope
}
```

### Deadlock Detection

```cpp
class DeadlockDetector {
private:
    struct LockInfo {
        std::thread::id thread_id;
        std::string lock_name;
        uint64_t timestamp_us;
    };
    
    mutable std::mutex detector_mutex_;
    std::unordered_map<std::string, LockInfo> held_locks_;
    std::unordered_map<std::thread::id, std::vector<std::string>> thread_locks_;
    
public:
    void RecordLockAcquire(const std::string& lock_name) {
        std::lock_guard<std::mutex> lock(detector_mutex_);
        
        auto thread_id = std::this_thread::get_id();
        auto timestamp = get_time_us();
        
        // Check for potential deadlock
        for (const auto& held_lock : thread_locks_[thread_id]) {
            if (held_lock > lock_name) {  // Lock ordering violation
                OutputDebugStringA(("POTENTIAL DEADLOCK: Thread acquiring " + 
                    lock_name + " while holding " + held_lock + "\n").c_str());
            }
        }
        
        held_locks_[lock_name] = {thread_id, lock_name, timestamp};
        thread_locks_[thread_id].push_back(lock_name);
    }
    
    void RecordLockRelease(const std::string& lock_name) {
        std::lock_guard<std::mutex> lock(detector_mutex_);
        
        auto thread_id = std::this_thread::get_id();
        
        held_locks_.erase(lock_name);
        auto& locks = thread_locks_[thread_id];
        locks.erase(std::find(locks.begin(), locks.end(), lock_name));
    }
};

// RAII lock wrapper with deadlock detection
template<typename Mutex>
class TrackedLock {
private:
    Mutex& mutex_;
    std::string name_;
    bool locked_;
    
public:
    TrackedLock(Mutex& m, const std::string& name) 
        : mutex_(m), name_(name), locked_(false) {
        g_deadlock_detector.RecordLockAcquire(name_);
        mutex_.lock();
        locked_ = true;
    }
    
    ~TrackedLock() {
        if (locked_) {
            mutex_.unlock();
            g_deadlock_detector.RecordLockRelease(name_);
        }
    }
};

#define TRACKED_LOCK(mutex, name) TrackedLock lock(mutex, name)
```

---

## 🎓 Best Practices Summary

### 1. Lock Hierarchy

**Always acquire locks in the same order:**

```cpp
// ✅ Good: Consistent lock ordering
class BridgeManager {
private:
    std::mutex clients_mutex_;      // Level 1
    std::mutex commands_mutex_;     // Level 2  
    std::mutex stats_mutex_;        // Level 3
    
public:
    void ComplexOperation() {
        std::lock_guard<std::mutex> lock1(clients_mutex_);     // Level 1 first
        std::lock_guard<std::mutex> lock2(commands_mutex_);    // Level 2 second
        std::lock_guard<std::mutex> lock3(stats_mutex_);       // Level 3 last
        
        // Do work with all locks held
    }
};
```

### 2. Minimize Lock Scope

**Hold locks for the shortest time possible:**

```cpp
// ❌ Bad: Lock held too long
void BadExample() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    expensive_computation();  // Don't hold lock during this
    shared_data.update();     // Only this needs protection
    more_expensive_work();    // Don't hold lock during this
}

// ✅ Good: Minimal lock scope
void GoodExample() {
    auto result = expensive_computation();  // No lock needed
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shared_data.update_with(result);  // Lock only what's needed
    }
    
    more_expensive_work();  // No lock needed
}
```

### 3. Prefer Lock-Free When Possible

**For high-frequency operations:**

```cpp
// ✅ Lock-free for counters and flags
std::atomic<uint64_t> frame_counter{0};
std::atomic<bool> shutdown_requested{false};

void HighFrequencyOperation() {
    frame_counter.fetch_add(1, std::memory_order_relaxed);  // Very fast
    
    if (shutdown_requested.load(std::memory_order_acquire)) {
        return;  // Fast check
    }
    
    // Continue with work
}
```

---

<!-- Removed "Next Steps" and cross-doc references to avoid broken links in public repo. Retained educational content above. -->