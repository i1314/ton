# TON 验证者发现与分片定向发送实现指南

## 1. Custom Overlay 部署指南

### 1.1 配置文件格式

创建 `/var/ton-work/db/custom-overlays.json`:

```json
{
  "overlays": [
    {
      "@type": "engine.validator.customOverlay",
      "name": "high-priority-ext-msgs",
      "nodes": [
        {
          "@type": "engine.validator.customOverlayNode",
          "adnl_id": "验证者1的ADNL ID（base64）",
          "msg_sender": true,
          "msg_sender_priority": 10,
          "block_sender": false
        },
        {
          "@type": "engine.validator.customOverlayNode", 
          "adnl_id": "验证者2的ADNL ID（base64）",
          "msg_sender": true,
          "msg_sender_priority": 10,
          "block_sender": false
        }
      ],
      "sender_shards": [
        {
          "@type": "tonNode.shardId",
          "workchain": 0,
          "shard": -9223372036854775808
        }
      ]
    }
  ]
}
```

### 1.2 部署步骤

```bash
# 1. 创建配置文件
cat > /var/ton-work/db/custom-overlays.json << EOF
{
  "overlays": [
    {
      "@type": "engine.validator.customOverlay",
      "name": "validator-fast-lane",
      "nodes": [],
      "sender_shards": []
    }
  ]
}
EOF

# 2. 通过validator-engine-console添加overlay
validator-engine-console -c "addcustomoverlay '{
  \"@type\": \"engine.validator.customOverlay\",
  \"name\": \"validator-fast-lane\",
  \"nodes\": [...],
  \"sender_shards\": []
}'"

# 3. 查看当前配置
validator-engine-console -c "showcustomoverlays"
```

## 2. 分片验证者解析器实现

```python
import hashlib
import struct
from typing import List, Dict, Tuple, Optional
import asyncio
import base64

class ShardIdFull:
    def __init__(self, workchain: int, shard: int):
        self.workchain = workchain
        self.shard = shard
    
    def to_str(self):
        return f"{self.workchain}:{self.shard:016x}"

class AccountAddress:
    def __init__(self, workchain: int, address: bytes):
        self.workchain = workchain
        self.address = address  # 32 bytes

class ValidatorInfo:
    def __init__(self, pubkey: bytes, weight: int, adnl_addr: Optional[bytes] = None):
        self.pubkey = pubkey
        self.weight = weight
        self.adnl_addr = adnl_addr

class ShardValidatorPRNG:
    """模拟TON的分片验证者伪随机生成器"""
    
    def __init__(self, shard: ShardIdFull, cc_seqno: int):
        # 使用分片ID和catchain序号作为种子
        seed_data = struct.pack('>iQI', shard.workchain, shard.shard, cc_seqno)
        self.hash = hashlib.sha512(seed_data).digest()
        self.pos = 0
        
    def next_ranged(self, range_max: int) -> int:
        """生成0到range_max-1的伪随机数"""
        if self.pos >= len(self.hash) - 8:
            # 重新生成哈希
            self.hash = hashlib.sha512(self.hash).digest()
            self.pos = 0
            
        # 读取8字节作为uint64
        value = struct.unpack('>Q', self.hash[self.pos:self.pos+8])[0]
        self.pos += 8
        
        # 映射到指定范围
        return (value * range_max) >> 64

class ShardValidatorResolver:
    """分片验证者解析器"""
    
    def __init__(self, lite_client_config: str):
        self.lite_client_config = lite_client_config
        self.current_validators = []  # List[ValidatorInfo]
        self.validator_set_hash = 0
        self.cc_seqno = 0
        self.shard_validators_cache = {}
        
    async def update_validator_set(self):
        """更新验证者集合"""
        # 获取配置参数34（当前验证者）
        config34 = await self.get_config_param(34)
        
        # 解析验证者列表
        self.current_validators = []
        total_weight = 0
        
        for validator_data in config34['validators']:
            pubkey = bytes.fromhex(validator_data['public_key'])
            weight = validator_data['weight']
            
            # 检查是否有ADNL地址
            adnl_addr = None
            if 'adnl_addr' in validator_data:
                adnl_hex = validator_data['adnl_addr']
                if adnl_hex and adnl_hex != '0' * 64:
                    adnl_addr = bytes.fromhex(adnl_hex)
            
            validator = ValidatorInfo(pubkey, weight, adnl_addr)
            self.current_validators.append(validator)
            total_weight += weight
        
        # 计算累积权重
        cumulative_weight = 0
        for validator in self.current_validators:
            cumulative_weight += validator.weight
            validator.cumulative_weight = cumulative_weight
        
        self.total_weight = total_weight
        self.cc_seqno = config34.get('catchain_seqno', 0)
        
        # 清空缓存
        self.shard_validators_cache.clear()
        
    def compute_shard_validators(self, shard: ShardIdFull, 
                               shard_validators_num: int = 7) -> List[ValidatorInfo]:
        """计算特定分片的验证者子集"""
        
        cache_key = (shard.workchain, shard.shard, self.cc_seqno)
        if cache_key in self.shard_validators_cache:
            return self.shard_validators_cache[cache_key]
        
        if shard.workchain == -1:  # 主链
            # 主链验证者是列表的前N个
            main_validators_num = min(16, len(self.current_validators))  # 通常是16个
            selected = self.current_validators[:main_validators_num]
        else:
            # 分片验证者通过伪随机选择
            prng = ShardValidatorPRNG(shard, self.cc_seqno)
            
            selected = []
            validators_copy = self.current_validators.copy()
            remaining_weight = self.total_weight
            
            for i in range(min(shard_validators_num, len(validators_copy))):
                if remaining_weight <= 0:
                    break
                    
                # 生成随机数选择验证者
                p = prng.next_ranged(remaining_weight)
                
                # 根据权重找到验证者
                cumulative = 0
                for idx, validator in enumerate(validators_copy):
                    cumulative += validator.weight
                    if p < cumulative:
                        selected.append(validator)
                        remaining_weight -= validator.weight
                        validators_copy.pop(idx)
                        
                        # 更新后续验证者的累积权重
                        for j in range(idx, len(validators_copy)):
                            validators_copy[j].cumulative_weight -= validator.weight
                        break
        
        self.shard_validators_cache[cache_key] = selected
        return selected
    
    def find_validator_by_cumulative_weight(self, weight: int) -> Optional[ValidatorInfo]:
        """根据累积权重查找验证者"""
        for validator in self.current_validators:
            if weight < validator.cumulative_weight:
                return validator
        return None
    
    async def get_config_param(self, param_id: int) -> dict:
        """获取配置参数（需要实现具体的lite-client调用）"""
        # 这里需要实现实际的lite-client调用
        # 示例返回格式
        return {
            'validators': [
                {
                    'public_key': 'abcd...',
                    'weight': 1000000,
                    'adnl_addr': 'ef01...'
                }
            ],
            'catchain_seqno': 100
        }
```

## 3. 智能消息路由器实现

```python
class SendStrategy:
    def __init__(self):
        self.targets = []  # 目标验证者列表
        self.redundancy = 1  # 冗余发送数量
        self.max_retries = 3
        self.retry_delay = 0.1

class SmartMessageRouter:
    """智能消息路由器"""
    
    def __init__(self, resolver: ShardValidatorResolver):
        self.resolver = resolver
        self.adnl_connections = {}  # ADNL连接池
        self.statistics = {
            'sent': 0,
            'success': 0,
            'failed': 0,
            'total_latency': 0
        }
        
    async def route_external_message(self, message: bytes, priority: int = 0) -> bool:
        """智能路由外部消息到负责的验证者"""
        
        start_time = asyncio.get_event_loop().time()
        
        try:
            # 1. 解析消息目标地址
            dest_addr = self.parse_destination_address(message)
            
            # 2. 计算目标分片
            target_shard = self.compute_target_shard(dest_addr)
            
            # 3. 获取该分片的验证者列表
            shard_validators = self.resolver.compute_shard_validators(target_shard)
            
            # 4. 准备发送策略
            send_strategy = self.prepare_send_strategy(shard_validators, priority)
            
            # 5. 执行发送
            success = await self.execute_send_strategy(message, send_strategy)
            
            # 更新统计
            self.statistics['sent'] += 1
            if success:
                self.statistics['success'] += 1
            else:
                self.statistics['failed'] += 1
            
            elapsed = asyncio.get_event_loop().time() - start_time
            self.statistics['total_latency'] += elapsed
            
            return success
            
        except Exception as e:
            print(f"Error routing message: {e}")
            self.statistics['failed'] += 1
            return False
    
    def parse_destination_address(self, message: bytes) -> AccountAddress:
        """解析外部消息的目标地址"""
        # 这里需要实现实际的消息解析
        # TON外部消息格式: ext_in_msg_info$10 src:MsgAddressExt dest:MsgAddressInt ...
        # 简化示例
        workchain = struct.unpack('>i', message[0:4])[0]
        address = message[4:36]
        return AccountAddress(workchain, address)
    
    def compute_target_shard(self, dest_addr: AccountAddress) -> ShardIdFull:
        """根据账户地址计算所属分片"""
        workchain = dest_addr.workchain
        
        if workchain == -1:  # 主链
            return ShardIdFull(workchain=-1, shard=0x8000000000000000)
        
        # 基础链的分片计算
        # TON使用账户地址的前缀来确定分片
        addr_prefix = int.from_bytes(dest_addr.address[:8], 'big')
        
        # 简化的分片计算（实际需要查询分片配置）
        # 这里假设只有一个基础分片
        return ShardIdFull(workchain=workchain, shard=0x8000000000000000)
    
    def prepare_send_strategy(self, validators: List[ValidatorInfo], 
                            priority: int) -> SendStrategy:
        """准备发送策略"""
        
        strategy = SendStrategy()
        
        # 筛选有ADNL地址的验证者
        validators_with_addr = [
            v for v in validators 
            if v.adnl_addr is not None
        ]
        
        if not validators_with_addr:
            # 如果没有公开地址，使用所有验证者
            validators_with_addr = validators
        
        # 根据优先级决定发送数量
        if priority >= 10:  # 高优先级
            # 发送给所有验证者
            strategy.targets = validators_with_addr
            strategy.redundancy = len(validators_with_addr)
            strategy.max_retries = 3
        elif priority >= 5:  # 中优先级
            # 发送给一半验证者
            half = max(1, len(validators_with_addr) // 2)
            strategy.targets = validators_with_addr[:half]
            strategy.redundancy = len(strategy.targets)
            strategy.max_retries = 2
        else:  # 普通优先级
            # 发送给3个验证者
            strategy.targets = validators_with_addr[:min(3, len(validators_with_addr))]
            strategy.redundancy = len(strategy.targets)
            strategy.max_retries = 1
        
        strategy.retry_delay = 0.1 if priority >= 10 else 0.5
        
        return strategy
    
    async def execute_send_strategy(self, message: bytes, 
                                  strategy: SendStrategy) -> bool:
        """执行发送策略"""
        
        if not strategy.targets:
            # 降级到广播模式
            return await self.broadcast_message(message)
        
        for attempt in range(strategy.max_retries):
            # 并发发送到多个验证者
            send_tasks = []
            for validator in strategy.targets[:strategy.redundancy]:
                task = self.send_to_validator(message, validator)
                send_tasks.append(task)
            
            # 等待结果
            results = await asyncio.gather(*send_tasks, return_exceptions=True)
            success_count = sum(1 for r in results if r is True)
            
            if success_count > 0:
                return True
            
            # 重试前等待
            if attempt < strategy.max_retries - 1:
                await asyncio.sleep(strategy.retry_delay)
        
        return False
    
    async def send_to_validator(self, message: bytes, 
                               validator: ValidatorInfo) -> bool:
        """发送消息到特定验证者"""
        
        try:
            # 如果验证者没有公开ADNL地址，使用公钥计算的地址
            if validator.adnl_addr:
                adnl_addr = validator.adnl_addr
            else:
                # 从公钥计算ADNL地址
                adnl_addr = hashlib.sha256(validator.pubkey).digest()
            
            # 这里需要实现实际的ADNL发送
            # 简化示例
            print(f"Sending to validator with ADNL: {adnl_addr.hex()[:16]}...")
            
            # 模拟发送
            await asyncio.sleep(0.01)
            
            return True
            
        except Exception as e:
            print(f"Failed to send to validator: {e}")
            return False
    
    async def broadcast_message(self, message: bytes) -> bool:
        """广播消息（降级方案）"""
        print("Broadcasting message through standard overlay...")
        # 实现标准overlay广播
        return True
    
    def get_statistics(self) -> dict:
        """获取统计信息"""
        total = self.statistics['sent']
        if total == 0:
            return {
                'success_rate': 0,
                'avg_latency': 0,
                'total_sent': 0
            }
        
        return {
            'success_rate': self.statistics['success'] / total,
            'avg_latency': self.statistics['total_latency'] / total,
            'total_sent': total,
            'total_success': self.statistics['success'],
            'total_failed': self.statistics['failed']
        }
```

## 4. 优化的Custom Overlay管理器

```python
class CustomOverlayManager:
    """Custom Overlay管理器"""
    
    def __init__(self, node_config_path: str):
        self.config_path = node_config_path
        self.overlays = {}
        
    async def create_shard_aware_overlay(self, 
                                       name: str,
                                       target_shards: List[ShardIdFull],
                                       resolver: ShardValidatorResolver) -> dict:
        """创建分片感知的overlay配置"""
        
        overlay_nodes = []
        added_validators = set()
        
        # 1. 为每个目标分片添加相关验证者
        for shard in target_shards:
            shard_validators = resolver.compute_shard_validators(shard)
            
            for validator in shard_validators:
                # 使用公钥作为唯一标识避免重复
                validator_id = validator.pubkey.hex()
                if validator_id in added_validators:
                    continue
                added_validators.add(validator_id)
                
                # 优先使用公开的ADNL地址
                if validator.adnl_addr:
                    adnl_id = base64.b64encode(validator.adnl_addr).decode()
                else:
                    # 从公钥计算ADNL ID
                    adnl_id = base64.b64encode(
                        hashlib.sha256(validator.pubkey).digest()
                    ).decode()
                
                overlay_nodes.append({
                    "@type": "engine.validator.customOverlayNode",
                    "adnl_id": adnl_id,
                    "msg_sender": False,  # 验证者不需要发送权限
                    "msg_sender_priority": 0,
                    "block_sender": False
                })
        
        # 2. 添加自己的节点（用于发送）
        my_adnl_id = await self.get_my_adnl_id()
        overlay_nodes.append({
            "@type": "engine.validator.customOverlayNode",
            "adnl_id": my_adnl_id,
            "msg_sender": True,  # 我们需要发送权限
            "msg_sender_priority": 10,  # 高优先级
            "block_sender": False
        })
        
        # 3. 创建overlay配置
        overlay_config = {
            "@type": "engine.validator.customOverlay",
            "name": name,
            "nodes": overlay_nodes,
            "sender_shards": [
                {
                    "@type": "tonNode.shardId",
                    "workchain": shard.workchain,
                    "shard": shard.shard
                }
                for shard in target_shards
            ]
        }
        
        self.overlays[name] = overlay_config
        return overlay_config
    
    async def deploy_overlay(self, overlay_config: dict) -> bool:
        """部署overlay到节点"""
        
        try:
            # 通过validator-engine-console部署
            import subprocess
            import json
            
            config_json = json.dumps(overlay_config)
            cmd = f'validator-engine-console -c "addcustomoverlay \'{config_json}\'"'
            
            result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
            
            if result.returncode == 0:
                print(f"Successfully deployed overlay: {overlay_config['name']}")
                return True
            else:
                print(f"Failed to deploy overlay: {result.stderr}")
                return False
                
        except Exception as e:
            print(f"Error deploying overlay: {e}")
            return False
    
    async def update_overlay_validators(self, 
                                      overlay_name: str,
                                      resolver: ShardValidatorResolver) -> bool:
        """更新overlay中的验证者列表"""
        
        if overlay_name not in self.overlays:
            print(f"Overlay {overlay_name} not found")
            return False
        
        overlay = self.overlays[overlay_name]
        target_shards = [
            ShardIdFull(s['workchain'], s['shard']) 
            for s in overlay['sender_shards']
        ]
        
        # 重新创建overlay配置
        new_config = await self.create_shard_aware_overlay(
            overlay_name, target_shards, resolver
        )
        
        # 先删除旧的，再添加新的
        await self.remove_overlay(overlay_name)
        return await self.deploy_overlay(new_config)
    
    async def remove_overlay(self, name: str) -> bool:
        """删除overlay"""
        
        try:
            cmd = f'validator-engine-console -c "delcustomoverlay {name}"'
            result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
            
            if result.returncode == 0:
                self.overlays.pop(name, None)
                return True
            return False
            
        except Exception as e:
            print(f"Error removing overlay: {e}")
            return False
    
    async def get_my_adnl_id(self) -> str:
        """获取本节点的ADNL ID"""
        # 这里需要实现获取本地节点ADNL ID的逻辑
        # 示例返回
        return base64.b64encode(b"my_adnl_id_placeholder").decode()
```

## 5. 完整使用示例

```python
async def main():
    """主函数示例"""
    
    # 1. 初始化组件
    resolver = ShardValidatorResolver("/path/to/lite-client-config.json")
    router = SmartMessageRouter(resolver)
    overlay_manager = CustomOverlayManager("/var/ton-work/db/")
    
    # 2. 定期更新验证者集合
    async def update_validators_task():
        while True:
            try:
                await resolver.update_validator_set()
                print(f"Updated validator set: {len(resolver.current_validators)} validators")
                
                # 更新custom overlay
                await overlay_manager.update_overlay_validators(
                    "high-priority-lane", resolver
                )
                
            except Exception as e:
                print(f"Error updating validators: {e}")
            
            await asyncio.sleep(60)  # 每分钟更新
    
    # 3. 创建高优先级overlay
    target_shards = [
        ShardIdFull(0, 0x8000000000000000),  # 基础链主分片
        ShardIdFull(-1, 0x8000000000000000)  # 主链
    ]
    
    overlay_config = await overlay_manager.create_shard_aware_overlay(
        "high-priority-lane",
        target_shards,
        resolver
    )
    
    success = await overlay_manager.deploy_overlay(overlay_config)
    if success:
        print("High priority overlay deployed successfully")
    
    # 4. 启动后台任务
    asyncio.create_task(update_validators_task())
    
    # 5. 发送高优先级消息示例
    async def send_high_priority_message(dest_addr: str, data: bytes):
        """发送高优先级消息"""
        
        # 构造外部消息
        # 这里需要实现实际的消息构造
        message = construct_external_message(dest_addr, data)
        
        # 智能路由发送
        success = await router.route_external_message(
            message=message,
            priority=10  # 高优先级
        )
        
        if success:
            print(f"✓ Message sent to validators of shard for {dest_addr}")
        else:
            print(f"✗ Failed to send message")
        
        return success
    
    # 6. 监控统计
    async def monitor_performance():
        while True:
            stats = router.get_statistics()
            
            print("\n=== Performance Stats ===")
            print(f"Success rate: {stats['success_rate']:.2%}")
            print(f"Average latency: {stats['avg_latency']:.3f}s")
            print(f"Total sent: {stats['total_sent']}")
            print(f"Active validators: {len(resolver.current_validators)}")
            
            # 显示各分片的验证者数量
            for wc in [0, -1]:
                shard = ShardIdFull(wc, 0x8000000000000000)
                validators = resolver.compute_shard_validators(shard)
                with_addr = sum(1 for v in validators if v.adnl_addr)
                print(f"Shard {shard.to_str()}: {len(validators)} validators ({with_addr} with ADNL)")
            
            await asyncio.sleep(30)
    
    asyncio.create_task(monitor_performance())
    
    # 7. 保持运行
    try:
        await asyncio.Event().wait()
    except KeyboardInterrupt:
        print("\nShutting down...")

def construct_external_message(dest_addr: str, data: bytes) -> bytes:
    """构造外部消息（需要实现）"""
    # 这里需要实现实际的TON外部消息构造
    # 包括正确的TL-B序列化
    return b"external_message_placeholder"

if __name__ == "__main__":
    asyncio.run(main())
```

## 6. 部署检查清单

### 6.1 前置条件
- [ ] TON全节点已同步并正常运行
- [ ] validator-engine-console可以正常连接
- [ ] Python 3.8+ 已安装
- [ ] 必要的Python包已安装（asyncio, aiohttp等）

### 6.2 部署步骤
1. [ ] 部署验证者发现服务
2. [ ] 配置custom overlay
3. [ ] 测试消息发送
4. [ ] 监控系统运行
5. [ ] 设置自动重启和日志

### 6.3 监控指标
- [ ] 验证者集合更新频率
- [ ] 消息发送成功率
- [ ] 平均延迟
- [ ] Overlay连接状态
- [ ] 错误率和错误类型

## 7. 故障排除

### 7.1 常见问题

**问题1**: 无法连接到验证者
- 检查验证者是否公开了ADNL地址
- 确认overlay配置正确
- 检查网络连接

**问题2**: 消息发送成功率低
- 增加冗余发送数量
- 检查消息格式是否正确
- 确认目标分片计算正确

**问题3**: 验证者集合更新失败
- 检查lite-client连接
- 确认配置参数34可访问
- 检查解析逻辑是否正确

### 7.2 调试建议
1. 启用详细日志记录
2. 使用测试网进行初步验证
3. 逐步增加负载测试
4. 监控资源使用情况

## 8. 安全注意事项

1. **不要尝试获取验证者真实IP**
2. **遵守网络礼仪，避免过度发送**
3. **保护自己的ADNL私钥**
4. **定期更新和审计代码**
5. **监控异常行为**

## 9. 性能优化建议

1. **连接池管理**
   - 维护长连接
   - 实现连接健康检查
   - 自动重连机制

2. **缓存策略**
   - 缓存分片验证者映射
   - 缓存消息路由决策
   - 定期刷新缓存

3. **批量处理**
   - 合并相同目标的消息
   - 实现消息队列
   - 优化发送时机

4. **资源控制**
   - 限制并发连接数
   - 实现背压机制
   - 监控内存使用

通过本指南的实现，您可以在仅运行全节点的情况下，实现高效的验证者发现和消息定向发送，显著提高消息上链速度。