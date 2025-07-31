# High-Performance IPC Service Architecture for MEV

## Overview
This document describes a redesigned IPC service optimized for MEV (Maximum Extractable Value) applications, where ultra-low latency is critical.

## Design Principles
1. **Zero-Copy**: Direct memory access without data copying
2. **Lock-Free**: Use atomic operations and ring buffers
3. **Nanosecond Precision**: High-resolution timestamps for accurate sequencing
4. **Separate Channels**: Isolated IPC files for external messages and blocks
5. **Raw Data**: No serialization overhead, direct byte streaming

## Architecture Components

### 1. Dual Channel Design
```
/tmp/ton-ipc-extmsg.sock   - External messages only
/tmp/ton-ipc-blocks.sock   - New blocks only
```

Benefits:
- No interference between message types
- Clients can subscribe to only what they need
- Independent scaling and optimization

### 2. Message Format (Binary)
```
[Header: 16 bytes]
- timestamp_ns: uint64 (8 bytes) - nanosecond timestamp
- data_length: uint32 (4 bytes)  - payload size
- message_type: uint8 (1 byte)   - for future extensibility
- reserved: uint8[3] (3 bytes)   - alignment padding

[Payload: variable]
- raw_data: bytes - Original TON data (td::BufferSlice)
```

### 3. Memory Architecture

#### Option A: Shared Memory Ring Buffer (Recommended for MEV)
```
Producer (TON Node) -> [Ring Buffer in SHM] -> Consumer (MEV Bot)
```
- Use shared memory segment with ring buffer
- Lock-free SPSC (Single Producer Single Consumer) queue
- mmap() for zero-copy access
- Configurable buffer size (e.g., 64MB)

#### Option B: Unix Domain Socket with Optimizations
```
Producer -> [Kernel Buffer] -> Consumer
```
- Set large socket buffers (SO_SNDBUF/SO_RCVBUF)
- Use MSG_ZEROCOPY flag (Linux 4.14+)
- Batch small messages with scatter-gather I/O

### 4. Time Synchronization
```cpp
struct HighResTimestamp {
    uint64_t nanoseconds;  // Since epoch
    uint32_t cpu_cycles;   // For sub-nanosecond ordering
};
```
- Use CLOCK_MONOTONIC_RAW to avoid NTP adjustments
- Record both wall time and CPU cycles
- Include timestamp at multiple points:
  - T1: Message received by node
  - T2: Message enters IPC queue
  - T3: Message sent to client

### 5. Hook Integration Points
```cpp
// Minimal overhead hooks
void onExternalMessage(const td::BufferSlice& data) {
    // Direct write to ring buffer
    // No allocation, no copying
}

void onNewBlock(const td::BufferSlice& data) {
    // Separate channel write
}
```

### 6. Client Library Design
- Memory-mapped file access
- Poll-based event loop (no blocking)
- Zero-allocation message processing
- Support for C++, Rust, Go

### 7. Performance Optimizations

#### CPU Affinity
- Pin IPC threads to specific cores
- Isolate from validator threads
- Use NUMA-aware allocation

#### Memory Layout
```
[Header Cache Line: 64 bytes]
[Payload aligned to cache line]
```

#### Batching Strategy
- Aggregate multiple small messages
- Use vectored I/O (writev/readv)
- Amortize system call overhead

### 8. Monitoring and Metrics
- Ring buffer utilization
- Message latency histogram
- Drop count (if buffer full)
- Throughput statistics

## Implementation Phases

### Phase 1: Core Infrastructure
- Ring buffer implementation
- Nanosecond timestamp
- Basic producer/consumer

### Phase 2: Channel Separation
- External message channel
- Block channel
- Raw data pass-through

### Phase 3: Optimizations
- Zero-copy mechanisms
- CPU affinity tuning
- Batch processing

### Phase 4: Client Libraries
- C++ native client
- Go client with cgo
- Rust client

## Performance Targets
- Latency: < 1 microsecond (hook to client)
- Throughput: > 1M messages/second
- CPU overhead: < 1% of single core
- Memory usage: Configurable ring buffer

## Security Considerations
- Unix socket permissions (600)
- No network exposure
- Read-only access for clients
- Separate processes for isolation

## Alternative Approaches Considered

### 1. io_uring (Linux 5.1+)
- Pros: True zero-copy, async I/O
- Cons: Linux-specific, complex implementation

### 2. RDMA/InfiniBand
- Pros: Hardware-accelerated, ultra-low latency
- Cons: Requires special hardware

### 3. eBPF
- Pros: In-kernel processing
- Cons: Limited functionality, security concerns

## Recommended Architecture
For MEV applications, we recommend:
1. Shared memory ring buffer for primary data path
2. Unix socket for control messages
3. Separate channels for external messages and blocks
4. Nanosecond timestamps with CPU cycle counter
5. Lock-free algorithms throughout

This design prioritizes latency over everything else, which is critical for MEV opportunities.