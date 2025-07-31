# IPC Service 异步架构说明

## 1. 完整区块数据推送

现在IPC服务支持推送完整的区块数据：

```cpp
// 在 full-node.cpp 中的钩子
ton_ipc_hooks::hookNewBlockWithData(
    broadcast.block_id.to_str(),     // 区块ID
    gen_utime,                       // 生成时间
    delay,                           // 延迟
    account_count,                   // 账户数量
    tx_count,                        // 交易数量
    tx_hashes,                       // 前5个交易哈希
    broadcast.data                   // 完整的原始区块数据
);
```

客户端接收到的 `BlockInfo` 结构包含：
- 基本信息（ID、时间、账户数、交易数等）
- 前5个交易的哈希值
- **完整的原始区块数据** (`RawBlockData` 字段)

## 2. 异步推送架构

### 架构图
```
TON Node Main Thread          IPC Service                    Clients
        |                          |                            |
        |--onNewBlock()----------->|                            |
        |  (non-blocking)          |                            |
        |<-return immediately------|                            |
        |                          |                            |
        |                    [Message Queue]                    |
        |                          |                            |
        |                    [Worker Thread 1]                  |
        |                          |--------send to client 1--->|
        |                          |                            |
        |                    [Worker Thread 2]                  |
        |                          |--------send to client 2--->|
```

### 关键特性

1. **非阻塞调用**
   ```cpp
   void onNewBlock(const BlockData& block) {
       impl_->onNewBlock(block, stats_);  // 立即返回
   }
   ```

2. **消息队列缓冲**
   ```cpp
   void enqueueMessage(MessageType type, std::vector<uint8_t>&& data, Stats& stats) {
       std::lock_guard<std::mutex> lock(queue_mutex_);
       if (message_queue_.size() >= config_.max_queue_size) {
           stats.dropped_messages++;  // 队列满时丢弃
           return;
       }
       message_queue_.push({type, std::move(data)});
       queue_cv_.notify_one();  // 通知工作线程
   }
   ```

3. **独立工作线程**
   - 默认2个工作线程处理消息广播
   - 工作线程从队列取消息并发送给客户端
   - 客户端连接失败自动清理

4. **性能保证**
   - 主线程调用钩子函数后立即返回
   - 不会因为客户端慢或断开而阻塞主线程
   - 队列满时自动丢弃消息，不影响节点运行

### 配置参数

```cpp
struct Config {
    std::string socket_path = "/tmp/ton-ipc.sock";
    size_t max_clients = 100;       // 最大客户端数
    size_t max_queue_size = 10000;  // 消息队列大小
    int worker_threads = 2;         // 工作线程数
};
```

## 3. 性能影响分析

1. **CPU影响**：极小
   - 主线程只做消息入队操作（微秒级）
   - 序列化和网络IO在独立线程中进行

2. **内存影响**：可控
   - 队列大小有上限（默认10000条）
   - 超出限制自动丢弃，不会无限增长

3. **不会阻塞节点**
   - 所有网络操作都是异步的
   - 客户端断开不影响其他客户端
   - 队列满时优先保证节点正常运行

## 4. 使用建议

1. **处理大量数据**
   - 如果只需要监控，不解析 `RawBlockData`
   - 客户端应该快速处理消息，避免积压

2. **调整参数**
   - 高负载时可以增加 `worker_threads`
   - 内存充足时可以增加 `max_queue_size`

3. **监控状态**
   ```go
   msgs, blocks, errors := client.GetStats()
   fmt.Printf("Dropped messages: %d\n", errors)
   ```