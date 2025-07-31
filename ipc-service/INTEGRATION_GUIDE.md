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
# Add IPC service
add_subdirectory(../ipc-service ipc-service)
target_link_libraries(validator-engine PUBLIC ton_ipc)
```

### 2. Initialize IPC Service

In `validator/validator-engine.cpp`, add at startup:

```cpp
#include "ipc-service/ton_node_hooks.h"

// In run() method, after other initializations:
if (!ton_ipc_hooks::initializeIPCService()) {
    LOG(WARNING) << "Failed to start IPC service";
}

// In destructor or shutdown:
ton_ipc_hooks::shutdownIPCService();
```

### 3. Hook External Messages

In `validator/full-node-shard.cpp` at line ~833 (after EXT_MSG_RECEIVED log):

```cpp
#include "ipc-service/ton_node_hooks.h"

// After the existing LOG(WARNING) for EXT_MSG_RECEIVED:
ton_ipc_hooks::hookExternalMessage(
    src,                                    // source address
    dest_addr,                              // destination address  
    query.message_->data_,                  // message data
    ""                                      // hash (optional)
);
```

### 4. Hook New Blocks

In `validator/full-node.cpp` at line ~735 (after block parsing):

```cpp
#include "ipc-service/ton_node_hooks.h"

// After successfully parsing block data:
ton_ipc_hooks::hookNewBlock(
    broadcast.block_id.to_str(),            // block ID
    gen_utime,                              // generation time
    delay,                                  // delay in seconds
    account_count,                          // number of accounts
    tx_count,                               // number of transactions
    tx_info                                 // transaction hashes (first 5)
);
```

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