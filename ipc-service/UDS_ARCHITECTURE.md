# Ultra-Fast UDS-based IPC Architecture for MEV

## Overview
Pure Unix Domain Socket (UDS) based design with separate .ipc files for external messages and blocks.

## Core Design

### 1. Dual UDS Channels
```
/tmp/ton-extmsg.ipc   - External messages only (SOCK_STREAM)
/tmp/ton-blocks.ipc   - New blocks only (SOCK_STREAM)
```

### 2. Why SOCK_STREAM?
- **Universal compatibility** - Works with all languages including Go
- **Reliable & ordered** - TCP-like semantics
- **Simple implementation** - Standard socket operations
- **Message framing** - Using fixed 16-byte header for boundaries
- **Cross-platform** - Same behavior on Linux and macOS

### 3. Message Format (Minimal Header)
```c
struct FastMessage {
    uint64_t timestamp_ns;  // 8 bytes - nanosecond timestamp
    uint32_t data_length;   // 4 bytes - payload size
    uint8_t  reserved[4];   // 4 bytes - alignment
    // Raw data follows immediately
} __attribute__((packed));

// Total overhead: 16 bytes only
```

### 4. Socket Optimizations

#### Buffer Sizes
```c
// Set large kernel buffers (32MB)
int bufsize = 32 * 1024 * 1024;
setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
```

#### Non-blocking Mode
```c
// Use epoll/kqueue for event-driven processing
fcntl(fd, F_SETFL, O_NONBLOCK);
```

#### Platform-Specific Optimizations
```c
// Linux: MSG_ZEROCOPY (kernel 4.14+)
send(fd, buf, len, MSG_ZEROCOPY);

// macOS: SO_NOSIGPIPE
int on = 1;
setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
```

### 5. Server Design (TON Node Side)

```cpp
class FastUDSServer {
    int extmsg_fd;
    int blocks_fd;
    
    void sendExternalMessage(const td::BufferSlice& data) {
        FastMessage msg;
        msg.timestamp_ns = getNanoTimestamp();
        msg.data_length = data.size();
        
        // Single system call with scatter-gather I/O
        struct iovec iov[2];
        iov[0].iov_base = &msg;
        iov[0].iov_len = sizeof(msg);
        iov[1].iov_base = (void*)data.data();
        iov[1].iov_len = data.size();
        
        writev(extmsg_fd, iov, 2);
    }
};
```

### 6. Client Design

```cpp
class FastUDSClient {
    int fd;
    epoll_fd;  // or kqueue on macOS
    
    void processMessages() {
        static thread_local uint8_t buffer[65536];
        
        while (true) {
            // Wait for data with epoll/kqueue
            int n = epoll_wait(epoll_fd, events, 1, 0);
            if (n <= 0) break;
            
            // Read complete message (SOCK_SEQPACKET guarantees this)
            ssize_t len = recv(fd, buffer, sizeof(buffer), 0);
            if (len <= sizeof(FastMessage)) continue;
            
            FastMessage* msg = (FastMessage*)buffer;
            uint8_t* data = buffer + sizeof(FastMessage);
            
            // Process with nanosecond timestamp
            onMessage(msg->timestamp_ns, data, msg->data_length);
        }
    }
};
```

### 7. Performance Tricks

#### CPU Affinity
```c
// Pin server thread to CPU 0
cpu_set_t cpuset;
CPU_ZERO(&cpuset);
CPU_SET(0, &cpuset);
pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);

// Pin client to CPU 1 (same NUMA node)
```

#### Busy Polling (Linux)
```c
// Enable busy polling for lower latency
int busy_poll = 50; // microseconds
setsockopt(fd, SOL_SOCKET, SO_BUSY_POLL, &busy_poll, sizeof(busy_poll));
```

#### Batched Sending
```cpp
// Accumulate multiple small messages
struct msghdr msg[10];
sendmmsg(fd, msg, 10, 0);
```

### 8. Hook Integration

```cpp
// Minimal overhead hooks
namespace ton_ipc_hooks {

inline void hookExternalMessage(const td::BufferSlice& data) {
    static thread_local FastUDSServer server("/tmp/ton-extmsg.ipc");
    server.sendExternalMessage(data);
}

inline void hookNewBlock(const td::BufferSlice& data) {
    static thread_local FastUDSServer server("/tmp/ton-blocks.ipc");
    server.sendBlock(data);
}

}
```

### 9. Why This Approach for MEV?

**Pros:**
- Simple & proven technology
- Kernel handles buffering
- Works on all Unix systems
- Good isolation between processes
- Easy to debug with standard tools

**Cons vs Shared Memory:**
- One extra copy (kernel buffer)
- System call overhead
- Slightly higher latency (1-5 microseconds vs <1 microsecond)

**But for MEV:**
- Still fast enough (microsecond level)
- More reliable than shared memory
- Easier to implement correctly
- Better process isolation

### 10. Expected Performance
- **Latency**: 1-5 microseconds (hook to client)
- **Throughput**: 500K-1M messages/second
- **CPU**: ~1-2% of single core
- **Memory**: Kernel buffer size (32MB per channel)

## Implementation Priority

1. **Phase 1**: Basic UDS with nanosecond timestamps
2. **Phase 2**: Separate channels for messages/blocks  
3. **Phase 3**: Platform optimizations (MSG_ZEROCOPY, etc)
4. **Phase 4**: Client libraries (C++, Go, Rust)

This UDS-based design balances performance with simplicity and reliability, making it ideal for MEV applications where microsecond-level latency is acceptable.