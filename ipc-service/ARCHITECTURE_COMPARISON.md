# IPC Architecture Comparison: UDS vs Shared Memory

## Quick Comparison Table

| Feature | Unix Domain Socket | Shared Memory |
|---------|-------------------|---------------|
| Latency | 1-5 μs | < 0.5 μs |
| Complexity | Simple | Complex |
| Reliability | High | Medium |
| Debugging | Easy | Hard |
| Cross-platform | Yes | Linux/macOS |
| Zero-copy | No (1 copy) | Yes |
| Process isolation | Excellent | Good |
| MEV suitable | Yes | Yes |

## Recommendation for Your Use Case

Given your requirements:
- **Separate .ipc files** ✓
- **Raw bytes transmission** ✓  
- **Nanosecond timestamps** ✓
- **MEV performance** ✓

**I recommend: Unix Domain Socket (UDS) with SOCK_SEQPACKET**

### Why?

1. **Simplicity**: Much easier to implement correctly
2. **Reliability**: Kernel handles all edge cases
3. **Fast enough**: 1-5μs latency is sufficient for MEV
4. **Standard tools**: Can use tcpdump, strace, etc.
5. **Your requirement**: You specifically mentioned ".ipc files"

### Optimized UDS Design for MEV

```
Architecture:
┌─────────────┐     UDS      ┌─────────────┐
│  TON Node   │ ──────────> │  MEV Bot    │
│             │ /tmp/*.ipc   │             │
└─────────────┘              └─────────────┘

Two separate channels:
- /tmp/ton-extmsg.ipc (external messages)
- /tmp/ton-blocks.ipc (new blocks)

Message format (16 bytes header + raw data):
[timestamp_ns:8][length:4][reserved:4][raw_data:N]
```

### Key Optimizations
1. **SOCK_SEQPACKET**: Message boundaries preserved
2. **Large buffers**: 32MB kernel buffers
3. **Vectored I/O**: writev() for single syscall
4. **CPU affinity**: Pin threads to cores
5. **Non-blocking**: epoll/kqueue for events

This gives you the best balance of:
- Performance (microsecond latency)
- Reliability (kernel-managed)
- Simplicity (standard UDS)
- Debugging (tcpdump, strace)