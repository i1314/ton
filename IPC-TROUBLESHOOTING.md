# IPC 故障排除指南

## 状态：连接成功但没有数据

### 可能原因：

1. **节点未完全同步**
   ```bash
   mytonctrl
   > status
   # 查看是否显示 "synchronized"
   ```

2. **网络活动较少**
   - 测试网活动比主网少
   - 可能需要等待 1-2 分钟才能看到新区块

3. **事件过滤**
   - 默认监控所有事件
   - 合约状态变化只在有交易时出现

### 诊断步骤：

1. **使用增强版调试客户端**
   ```bash
   python3 ~/ton/validator/ipc-client-debug.py
   ```
   这会显示：
   - 连接状态
   - 运行时间统计
   - 每 30 秒的状态报告

2. **检查节点日志中的 IPC 消息**
   ```bash
   sudo journalctl -u ton -n 100 | grep -i "ipc"
   ```

3. **验证钩子是否工作**
   ```bash
   # 检查二进制文件中的符号
   nm /usr/local/bin/validator-engine | grep -i publish
   ```

4. **手动触发事件**
   - 发送测试交易
   - 或等待新区块（主网约 5 秒/块）

### 预期输出示例：

```
✓ Connected to /tmp/ton-ipc.sock
  Time: 2024-01-20 15:30:45
  Waiting for events...

[15:30:52] [New Block #1]
  Block ID: (-1,8000000000000000,12345)
  Seqno: 12345, Workchain: -1, Shard: -9223372036854775808

[15:30:57] [New Block #2]
  Block ID: (-1,8000000000000000,12346)
  Seqno: 12346, Workchain: -1, Shard: -9223372036854775808
```

### 如果长时间没有数据：

1. **确认编译选项**
   ```bash
   grep "TON_IPC_ENABLED" ~/ton/build/CMakeCache.txt
   # 应该显示: TON_IPC_ENABLED:BOOL=ON
   ```

2. **重新编译确保最新代码**
   ```bash
   cd ~/ton
   git pull
   cd build
   make clean
   cmake .. -DTON_IPC_ENABLED=ON
   make -j$(nproc) validator-engine
   ```

3. **检查 Actor 系统**
   IPC Publisher 作为 Actor 运行，可能需要检查 Actor 是否正常启动。

### 测试环境建议：

如果在测试网，活动可能很少。建议：
1. 连接到主网进行测试（会有更多活动）
2. 或者设置一个定期发送交易的脚本

### 日志级别调整：

如果需要更详细的日志，可以修改 `validator/ipc-publisher.cpp`，添加更多 LOG 语句。