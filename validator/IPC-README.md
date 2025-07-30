# TON IPC 钩子实现说明

## 钩子位置

### 1. 外部消息钩子
- 文件: `validator/manager.cpp`
- 函数: `ValidatorManagerImpl::send_external_message()`
- 行号: 约3380行
- 时机: 从网络接收到外部消息时

### 2. 新区块钩子
- 文件: `validator/full-node.cpp`
- 函数: `FullNodeImpl::process_block_broadcast()`
- 行号: 约640行
- 时机: 从网络接收到新区块广播时（最早时机）

### 3. 合约状态变化钩子
- 文件: `validator/impl/collator.cpp`
- 函数: 合约账户处理循环
- 行号: 约3010行
- 时机: 检测到账户状态哈希变化时

## 数据格式（当前简化版）
- 使用简单字符串格式，用分隔符分隔字段
- 格式: `TYPE|FIELD1|FIELD2|...`

## 编译选项
- CMake选项: `-DTON_IPC_ENABLED=ON`
- 宏定义: `TON_IPC_ENABLED`

## TODO
1. 优化序列化格式
2. 添加更多字段信息
3. 实现地址过滤功能