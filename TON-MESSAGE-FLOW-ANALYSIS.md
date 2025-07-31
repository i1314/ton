# TON 外部消息流转机制深度分析报告

## 目录
1. [外部消息从RPC到区块打包的完整流程](#1-外部消息从rpc到区块打包的完整流程)
2. [消息转发机制和加速上链策略](#2-消息转发机制和加速上链策略)
3. [外部消息打包规则详解](#3-外部消息打包规则详解)
4. [内部消息流转和排序规则](#4-内部消息流转和排序规则)
5. [自定义Overlay网络加速方案](#5-自定义overlay网络加速方案)
6. [验证者发现和连接实现方案](#6-验证者发现和连接实现方案)
7. [分片验证者定向发送实现方案](#7-分片验证者定向发送实现方案)

## 1. 外部消息从RPC到区块打包的完整流程

### 1.1 RPC接收阶段
- **入口点**: `LiteQuery::perform_sendMessage()` (liteserver.cpp:549)
- 客户端通过 `liteServer_sendMessage` RPC调用发送外部消息
- 消息先经过 `ValidatorManager::check_external_message()` 验证

### 1.2 消息验证阶段
- **位置**: `ValidatorManagerImpl::check_external_message()` (manager.cpp:443)
- **验证步骤**:
  - 检查节点同步状态
  - 解析消息格式（通过 `create_ext_message()`）
  - 限流检查：每个地址的消息频率限制
  - 运行智能合约验证（通过 `run_check_external_message()`）

### 1.3 消息分发阶段
- 验证通过后，调用 `ValidatorManager::send_external_message()` (manager.cpp:1819)
- 消息被广播到P2P网络：`callback_->send_ext_message()`
- 同时加入本地优先级队列：`add_external_message(message, priority)` (manager.cpp:421)

### 1.4 消息存储结构
- 外部消息存储在 `ext_msgs_[priority]` 多优先级队列中
- 每个地址限制256条消息（`per_address_limit`）
- 消息去重通过 `ext_messages_hashes_` 哈希表实现

### 1.5 Collator打包阶段
- **入口**: `Collator::process_inbound_external_messages()` (collator.cpp:4141)
- **打包规则**:
  - 跳过条件：当 `out_msg_queue_size_ > 8000` 时，只处理高优先级消息
  - 高优先级定义：`priority >= 10` (HIGH_PRIORITY_EXTERNAL)
  - 区块满检查：通过 `block_limit_status_->fits()` 判断
  - 超时检查：`medium_timeout_` 限制处理时间

## 2. 消息转发机制和加速上链策略

### 2.1 当前转发机制
- 消息通过 `FullNodeShard::send_external_message()` 在分片间转发
- 支持自定义overlay网络进行特殊路由
- 广播使用 `tonNode_externalMessageBroadcast` TL对象

### 2.2 加速上链的可能性

#### a) 多节点并行发送策略
- 同时向多个验证者节点发送外部消息
- 利用自定义overlay网络定向发送到目标分片验证者
- 可减少网络跳数，提高消息到达验证者的概率

#### b) 优先级机制优化
- 当前只有两个优先级级别（普通和高优先级）
- 可扩展为多级优先级，根据gas费用动态调整
- 高优先级阈值可配置（当前固定为10）

#### c) 预验证和缓存
- LiteServer已实现消息缓存机制（`LiteServerCache`）
- 可在更多节点部署预验证，减少重复验证开销

## 3. 外部消息打包规则详解

### 3.1 优先级机制
```cpp
static constexpr int HIGH_PRIORITY_EXTERNAL = 10;
if (out_msg_queue_size_ > SKIP_EXTERNALS_QUEUE_SIZE && 
    ext_msg_struct.priority < HIGH_PRIORITY_EXTERNAL) {
    continue; // 跳过低优先级消息
}
```

### 3.2 队列大小限制
- `SKIP_EXTERNALS_QUEUE_SIZE = 8000`：触发优先级过滤的阈值
- 当输出消息队列超过8000时，只处理高优先级外部消息

### 3.3 区块容量限制
- 使用 `block_limit_status_` 跟踪区块使用情况
- 软限制（cl_soft）：触发停止处理外部消息
- 支持gas、字节大小、消息数等多维度限制

### 3.4 重试机制
- `attempt_idx_ >= 2` 时完全跳过外部消息
- 失败的消息加入 `bad_ext_msgs_` 列表
- 延迟的消息加入 `delay_ext_msgs_` 列表

## 4. 内部消息流转和排序规则

### 4.1 内部消息生成
- 外部消息执行后生成的内部消息通过 `register_new_msg()` 注册
- 存储在优先队列 `new_msgs` 中，按逻辑时间(lt)排序

### 4.2 消息路由
- 使用超立方体路由算法：`perform_hypercube_routing()`
- 计算源到目标的最优路径
- 生成 `MsgEnvelope` 包含路由信息

### 4.3 Dispatch Queue机制
- 延迟消息存储在 `dispatch_queue_` 中
- 三阶段处理策略：
  - 阶段1：无限制处理
  - 阶段2：限制每个发起者的消息数
  - 阶段3：更严格的限制，根据队列大小动态调整

### 4.4 内部消息排序规则
- 主要按逻辑时间(lt)排序
- 同一账户的消息保持FIFO顺序
- Dispatch queue支持优先账户列表（prioritylist）

## 5. 自定义Overlay网络加速方案

### 5.1 CustomOverlay功能特性
```cpp
struct CustomOverlayParams {
  std::string name_;
  std::vector<adnl::AdnlNodeIdShort> nodes_;          // 参与节点列表
  std::map<adnl::AdnlNodeIdShort, int> msg_senders_;  // 消息发送权限映射
  std::set<adnl::AdnlNodeIdShort> block_senders_;     // 区块发送权限
  std::vector<ShardIdFull> sender_shards_;            // 发送分片限制
};
```

### 5.2 外部消息定向发送机制
- 在 `FullNodeImpl::send_ext_message()` 中检查custom overlays (full-node.cpp:326)
- 只有在 `msg_senders_` 白名单中的节点才能发送消息
- 支持按分片过滤：`params_.send_shard(dst.as_leaf_shard())`

### 5.3 实现快速上链的具体方案

#### a) 创建验证者专用通道
- 建立包含当前验证者集合的custom overlay
- 设置高优先级消息发送权限
- 直接路由到目标分片验证者

#### b) 消息优先级扩展
- 当前msg_senders_支持int类型值，可用作优先级
- 可扩展为动态费用竞价系统
- 根据gas价格自动调整转发优先级

## 6. 验证者发现和连接实现方案

### 6.1 TON的验证者地址设计

从代码分析可以看到，TON的验证者描述有两种格式：

```tlb
validator#53 public_key:SigPubKey weight:uint64 = ValidatorDescr;
validator_addr#73 public_key:SigPubKey weight:uint64 adnl_addr:bits256 = ValidatorDescr;
```

- **validator（0x53）**：只包含公钥和权重，不包含网络地址
- **validator_addr（0x73）**：包含公钥、权重和ADNL地址

### 6.2 实际暴露情况
1. 验证者可以选择是否公开其真实ADNL地址
2. 如果不公开（addr为零），系统会从公钥计算一个默认地址
3. 大多数验证者出于安全考虑，不会公开真实服务地址

### 6.3 TON的安全设计

#### 多层网络架构
```
┌─────────────────────┐
│   公开层            │ ← 验证者公钥（必须公开）
├─────────────────────┤
│   半公开层          │ ← ADNL地址（可选公开）
├─────────────────────┤
│   私有层            │ ← 真实IP地址（永不公开）
└─────────────────────┘
```

#### ADNL协议保护
- ADNL是TON的匿名网络层协议
- 即使知道ADNL地址，也无法直接获取验证者的真实IP
- 通信通过DHT和overlay网络中转

### 6.4 最优实现方案

#### 建立高性能中继架构
```
┌─────────────────┐     ┌─────────────────┐
│  您的全节点      │────▶│  中继节点集群    │
└─────────────────┘     └────────┬────────┘
                                 │
                    ┌────────────┴────────────┐
                    ▼                         ▼
            ┌──────────────┐          ┌──────────────┐
            │ 标准Overlay  │          │ Custom       │
            │ (冗余路径)   │          │ Overlay      │
            └──────────────┘          └──────────────┘
                    │                         │
                    └────────────┬────────────┘
                                 ▼
                         ┌──────────────┐
                         │  验证者节点   │
                         │ (间接到达)   │
                         └──────────────┘
```

## 7. 分片验证者定向发送实现方案

### 7.1 核心逻辑分析

TON的验证者分配算法：

```cpp
// 主链验证者：直接取列表前N个
if (is_mc) {
    for (unsigned i = 0; i < count; i++) {
        nodes.emplace_back(v.pubkey, v.weight, v.adnl_addr);
    }
}

// 分片验证者：通过伪随机算法选择
else {
    // 使用分片ID和catchain序号生成随机数
    ValidatorSetPRNG gen{shard, cc_seqno};
    // 从总验证者集合中随机选择子集
    for (unsigned i = 0; i < count; i++) {
        auto p = gen.next_ranged(total_wt);
        auto& entry = vset.at_weight(p);
        nodes.emplace_back(entry.pubkey, 1, entry.adnl_addr);
    }
}
```

### 7.2 实现分片验证者发现

```python
class ShardValidatorResolver:
    """分片验证者解析器"""
    
    def compute_shard_validators(self, shard: ShardIdFull) -> List[ValidatorInfo]:
        """计算特定分片的验证者子集"""
        
        # 模拟TON的ValidatorSetPRNG算法
        prng = ShardValidatorPRNG(shard, self.cc_seqno)
        
        # 获取验证者数量配置
        shard_val_num = self.get_shard_validator_num()  # 通常是7-13个
        
        selected_validators = []
        for i in range(shard_val_num):
            # 生成伪随机数选择验证者
            p = prng.next_ranged(total_weight)
            # 根据权重找到对应的验证者
            validator = self.find_validator_by_weight(p)
            selected_validators.append(validator)
        
        return selected_validators
```

### 7.3 智能消息路由实现

```python
class SmartMessageRouter:
    """智能消息路由器"""
    
    async def route_external_message(self, message: bytes, priority: int = 0):
        """智能路由外部消息到负责的验证者"""
        
        # 1. 解析消息目标地址
        dest_addr = self.parse_destination_address(message)
        
        # 2. 计算目标分片
        target_shard = self.compute_target_shard(dest_addr)
        
        # 3. 获取该分片的验证者列表
        shard_validators = self.resolver.compute_shard_validators(target_shard)
        
        # 4. 准备发送策略
        send_strategy = self.prepare_send_strategy(shard_validators, priority)
        
        # 5. 执行发送
        return await self.execute_send_strategy(message, send_strategy)
```

### 7.4 关键优势

1. **精准投递**：只发送给负责处理该地址的验证者
2. **减少网络负载**：避免全网广播
3. **提高成功率**：直接到达处理节点
4. **灵活冗余**：根据优先级调整发送数量
5. **动态适应**：自动跟踪验证者轮换

## 优化建议总结

### 1. 提高外部消息上链速度
- 实现智能路由：根据账户地址直接路由到负责验证的节点
- 增加更多优先级级别，支持动态费用竞价
- 优化网络拓扑，减少消息转发跳数

### 2. 全节点加速策略
- 部署专用的外部消息转发节点
- 实现消息预验证池，减少重复验证
- 使用持久连接维护到验证者的快速通道

### 3. 内部消息优化
- 全节点无法直接影响内部消息处理顺序
- 但可通过监控dispatch queue状态，智能调度外部消息发送时机
- 实现队列拥塞预测，避免在高峰期发送低优先级消息

### 4. 监控和反馈机制
- 实时监控各分片的消息队列状态
- 根据队列长度动态调整消息发送策略
- 实现消息状态追踪，及时重试失败的消息

## 实施路线图

### 第一阶段：基础设施
- 部署custom overlay网络
- 实现验证者发现和连接
- 建立监控系统

### 第二阶段：智能路由
- 实现分片感知路由
- 动态验证者追踪
- 优先级队列管理

### 第三阶段：高级优化
- 实现竞价系统
- 批量处理优化
- 预测性缓存

通过以上分析和优化方案，可以显著提高TON网络中外部消息的上链速度，特别是对于高价值、时间敏感的交易场景。Custom overlay网络提供了灵活的扩展机制，使得各种优化策略得以实现。