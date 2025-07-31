# TON IPC Service Integration Guide

## Overview

The TON IPC Service provides a high-performance, minimal-intrusion subscription mechanism for external messages and new blocks in the TON validator node.

## Features

- **Minimal Code Intrusion**: Only requires adding 3-4 lines of code at hook points
- **High Performance**: Lock-free message queue, multiple worker threads
- **Security**: Unix domain sockets with client isolation
- **Language Support**: C++ server with Go client library
- **Account State Updates**: Block data includes account state changes via `state_update` field

## Architecture

```
TON Node -> IPC Service (C++) -> Unix Socket -> Go Client
                |
                +-> Message Queue -> Worker Threads -> Connected Clients
```

## Building Independently

```bash
cd ipc-service
./build_and_test.sh
```

## Integration Steps

### 1. Add IPC Service to TON Build

Add to `validator/CMakeLists.txt`:

```cmake
# Add IPC service (now included by default)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../ipc-service ${CMAKE_CURRENT_BINARY_DIR}/ipc-service)
target_link_libraries(validator-engine PUBLIC ton_ipc)
```

### 2. Automatic Initialization

**No manual initialization needed!** The IPC service automatically starts on first use.

The service will:
- Auto-start when the first message is sent
- Auto-stop when the process exits
- Use default socket path `/tmp/ton-ipc.sock`

Optional: You can still manually control the service if needed:
```cpp
// Optional: custom initialization
ton_ipc_hooks::initializeIPCService("/custom/path.sock");

// Optional: manual shutdown
ton_ipc_hooks::shutdownIPCService();
```

### 3. Hooks Already Integrated!

The hooks are already added to the TON node code:

**External Messages** (in `validator/full-node-shard.cpp`):
```cpp
// Already integrated - captures all external messages
try {
  ton_ipc_hooks::hookExternalMessage(src, dest_addr, query.message_->data_, "");
} catch (...) {
  // Errors are ignored to not affect node operation
}
```

**New Blocks** (in `validator/full-node.cpp`):
```cpp
// Already integrated - captures all new blocks with full data
try {
  ton_ipc_hooks::hookNewBlockWithData(
    broadcast.block_id.to_str(), gen_utime, delay, 
    account_count, tx_count, tx_hashes, 
    broadcast.data  // Full block data
  );
} catch (...) {
  // Errors are ignored to not affect node operation
}
```

No additional code changes needed!

## Go Client Usage

```go
package main

import (
    "fmt"
    "log"
    ton_ipc "github.com/ton-blockchain/ton/ipc-service/client/go"
)

func main() {
    client := ton_ipc.NewClient("/tmp/ton-ipc.sock")
    
    // Set handlers
    client.SetExternalMessageHandler(func(msg *ton_ipc.ExternalMessage) {
        fmt.Printf("External message: %s -> %s\n", msg.SourceAddr, msg.DestAddr)
    })
    
    client.SetNewBlockHandler(func(block *ton_ipc.BlockInfo) {
        fmt.Printf("New block: %s, txs: %d\n", block.BlockID, block.TransactionCount)
    })
    
    // Connect and subscribe
    if err := client.Connect(); err != nil {
        log.Fatal(err)
    }
    defer client.Close()
    
    client.Subscribe(ton_ipc.SubscriptionAll)
    
    // Keep running...
    select {}
}
```

## Performance Considerations

- Message queue is lock-free for enqueue operations
- Multiple worker threads handle client broadcasts
- Failed client connections are automatically cleaned up
- Messages are dropped if queue is full (configurable limit)

## Security

- Unix domain sockets provide process-level isolation
- No network exposure by default
- Client permissions controlled by socket file permissions
- Each client connection is isolated

## Block Data and Account States

The `AccountBlock` structure in TON blocks contains:
- `account_addr`: Account address (256 bits)
- `transactions`: Map of transactions by logical time
- `state_update`: HASH_UPDATE containing the account state changes

This means subscribers receive notifications when accounts change state, making it suitable for:
- Monitoring contract deployments
- Tracking account balance changes
- Detecting contract state updates

## Testing

Run the test server and client:

```bash
# Terminal 1 - Run test server
cd ipc-service/build
./test_server

# Terminal 2 - Run Go client
cd ipc-service/client/go/example
go run main.go
```

## Monitoring

The IPC service provides statistics via `getStats()`:
- Messages sent
- Blocks sent  
- Active clients
- Dropped messages

## Troubleshooting

1. **Permission Denied**: Check socket file permissions (`/tmp/ton-ipc.sock`)
2. **Connection Refused**: Ensure IPC service is running
3. **Messages Dropped**: Increase `max_queue_size` in config
4. **High CPU Usage**: Reduce `worker_threads` count