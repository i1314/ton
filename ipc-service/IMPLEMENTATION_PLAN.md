# IPC Service Implementation Plan

## Quick Summary
- **Goal**: Ultra-low latency IPC for MEV
- **Approach**: Shared memory ring buffer with nanosecond timestamps
- **Channels**: Separate .ipc files for external messages and blocks
- **Format**: Raw bytes with minimal header

## Core Design Decisions

### 1. Transport Layer
**Shared Memory with Ring Buffer** (Primary)
- `/dev/shm/ton-extmsg-ring` - External messages
- `/dev/shm/ton-blocks-ring` - Blocks
- Lock-free SPMC (Single Producer Multiple Consumer)
- 64MB default size per channel

**Unix Domain Socket** (Fallback/Control)
- `/tmp/ton-ipc-control.sock` - Control commands
- Used for client registration and stats

### 2. Data Format
```c
// 24-byte header (fits in CPU cache line)
struct MessageHeader {
    uint64_t timestamp_ns;    // 8 bytes - nanoseconds since epoch
    uint64_t sequence_num;    // 8 bytes - monotonic sequence
    uint32_t data_length;     // 4 bytes - payload size  
    uint16_t type;           // 2 bytes - message type
    uint16_t flags;          // 2 bytes - future use
} __attribute__((packed));

// Followed immediately by raw data
```

### 3. Ring Buffer Structure
```c
struct RingBuffer {
    // Cache line 1: Producer
    alignas(64) atomic<uint64_t> write_pos;
    uint8_t pad1[56];
    
    // Cache line 2: Consumers  
    alignas(64) atomic<uint64_t> read_pos[MAX_CONSUMERS];
    
    // Cache line 3: Metadata
    alignas(64) uint32_t size;
    uint32_t consumer_count;
    char name[56];
    
    // Data area
    alignas(64) uint8_t data[];
};
```

### 4. Hook Integration
```cpp
// In ton_node_hooks.h
namespace ton_ipc {
    
class FastIPC {
    RingBuffer* extmsg_ring;
    RingBuffer* blocks_ring;
    
    void writeExternalMessage(const td::BufferSlice& data) {
        auto now = chrono::high_resolution_clock::now();
        uint64_t ns = chrono::duration_cast<chrono::nanoseconds>(
            now.time_since_epoch()).count();
            
        // Direct write to ring buffer
        writeToRing(extmsg_ring, ns, data.data(), data.size());
    }
};

// Global instance
inline FastIPC& getIPC() {
    static FastIPC instance;
    return instance;
}

// Hooks
inline void hookExternalMessage(const td::BufferSlice& data) {
    getIPC().writeExternalMessage(data);
}

inline void hookNewBlock(const td::BufferSlice& data) {
    getIPC().writeBlock(data);
}

}
```

### 5. Client API Example
```cpp
// C++ Client
class IPCReader {
    RingBuffer* ring;
    uint64_t last_seq = 0;
    
    bool readNext(MessageHeader& header, vector<uint8_t>& data) {
        return ring->tryRead(consumer_id, header, data);
    }
};

// Usage
IPCReader reader("/dev/shm/ton-extmsg-ring");
MessageHeader header;
vector<uint8_t> data;

while (reader.readNext(header, data)) {
    // Process with nanosecond timestamp
    processMessage(header.timestamp_ns, data);
}
```

### 6. Performance Features
- **Zero allocation**: Pre-allocated ring buffers
- **Zero copy**: Direct memory access via mmap
- **Cache-friendly**: Aligned data structures
- **Wait-free**: No locks, only atomics
- **Batching**: Read multiple messages at once

### 7. File Structure
```
ipc-service/
├── include/
│   ├── fast_ipc.h         # Main API
│   ├── ring_buffer.h      # Lock-free ring buffer
│   └── timestamp.h        # High-res timing
├── src/
│   ├── fast_ipc.cpp       # Core implementation
│   ├── ring_buffer.cpp    # Ring buffer logic
│   └── shm_manager.cpp    # Shared memory management
├── clients/
│   ├── cpp/              # C++ client library
│   ├── rust/             # Rust client (future)
│   └── go/               # Go client (cgo)
└── benchmarks/
    └── latency_test.cpp   # Performance testing
```

## Next Steps

1. **Implement ring buffer** with lock-free algorithms
2. **Create shared memory manager** for lifecycle
3. **Add nanosecond timestamps** with CPU cycle backup
4. **Integrate hooks** with minimal overhead
5. **Build test harness** for latency measurement
6. **Create client libraries** for each language

## Key Metrics to Track
- Hook-to-client latency: Target < 500ns
- Message throughput: Target > 2M msg/s
- CPU usage: Target < 0.5% per core
- Memory usage: Fixed at buffer size

## Risks and Mitigations
- **Risk**: Shared memory corruption
  - **Mitigation**: Use separate processes, validate pointers
- **Risk**: Consumer falling behind
  - **Mitigation**: Ring buffer overflow detection
- **Risk**: Platform compatibility
  - **Mitigation**: Fallback to sockets on unsupported systems