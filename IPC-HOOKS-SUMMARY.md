# TON IPC Hooks Summary

## Hook Locations with Call Chain

### 1. External Messages (从网络接收)
**Hook Location**: `validator/full-node-shard.cpp`
```cpp
void FullNodeShardImpl::process_broadcast(PublicKeyHash src, ton_api::tonNode_externalMessageBroadcast &query)
```

**Call Chain**:
1. Overlay network receives broadcast
2. `OverlayImpl::receive_broadcast()` → `callback_->receive_broadcast()`
3. `FullNodeShardImpl::receive_broadcast()` - parses TL object
4. `FullNodeShardImpl::process_broadcast(externalMessageBroadcast)` - **HOOK HERE**
5. `ValidatorManagerInterface::new_external_message()` - forwards to validator

### 2. New Blocks (最早接收时机)
**Hook Location**: `validator/full-node.cpp`
```cpp
void FullNodeImpl::process_block_broadcast(BlockBroadcast broadcast)
```

**Call Chain**:
1. Overlay network receives block broadcast
2. `FullNodeShardImpl::receive_broadcast()` - receives raw broadcast
3. `FullNodeShardImpl::process_block_broadcast()` - deserializes block
4. `FullNodeImpl::process_block_broadcast()` - **HOOK HERE**
5. `ValidatorManagerInterface::new_block_broadcast()` - forwards to validator

### 3. Contract State Changes
**Hook Location**: `validator/impl/collator.cpp`
```cpp
// In account processing loop, after computing new state hash
if (acc.total_state->get_hash() != acc.orig_total_state->get_hash()) {
  // HOOK HERE
}
```

## Trace Logging
All hooks include `[IPC-TRACE]` prefixed logs to track the call chain:
- Function entry/exit
- Source information
- Data size/content
- Forwarding destinations

## Compilation
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DTON_IPC_ENABLED=ON
make -j$(nproc) validator-engine
```

## Testing
1. Run validator with IPC enabled
2. Look for `[IPC-TRACE]` logs to verify hooks are called
3. Connect IPC client to `/tmp/ton-ipc.sock`