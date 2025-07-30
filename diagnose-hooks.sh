#!/bin/bash
# 诊断钩子问题

echo "=== 诊断 TON IPC 钩子 ==="
echo

echo "1. 检查是否有区块相关的函数调用："
sudo grep -h "process_block\|new_block\|apply_block\|got_block" /var/ton-work/log.thread*.log | tail -50 | grep -v "STATUS:"

echo
echo "2. 检查是否有外部消息："
sudo grep -h "external_message\|ext_message\|send_external" /var/ton-work/log.thread*.log | tail -20

echo
echo "3. 检查节点同步状态（从最新日志）："
latest_log=$(ls -t /var/ton-work/log.thread*.log | head -1)
echo "从 $latest_log:"
sudo grep "last_masterchain_block_ago" "$latest_log" | tail -5

echo
echo "4. 检查是否有验证器活动："
sudo grep -h "validator\|ValidatorManager" /var/ton-work/log.thread*.log | grep -v "STATUS:" | tail -20

echo
echo "5. 查看 IPCPublisher 的实际日志："
sudo grep -h "IPCPublisher\|IPC.*start_up\|socket.*created" /var/ton-work/log.thread*.log | tail -20

echo
echo "6. 检查线程信息："
echo "IPC 初始化在线程: t62"
echo "最新活动在线程: t14"
echo
echo "查看 t62 线程的更多日志："
sudo grep "t62" /var/ton-work/log.thread62.log | tail -20

echo
echo "7. 检查是否编译了正确的代码："
echo "查找我们添加的日志标记："
strings /usr/local/bin/validator-engine | grep -E "process_block_broadcast called|Publishing new block event|ValidatorManager::new_block_broadcast called" | head -10

echo
echo "=== 诊断结果 ==="
if ! strings /usr/local/bin/validator-engine | grep -q "process_block_broadcast called"; then
    echo "❌ 二进制文件中没有找到我们添加的日志！需要重新编译最新代码。"
else
    echo "✓ 二进制文件包含我们的日志代码"
fi

echo
echo "建议："
echo "1. 如果没有看到钩子日志，说明需要更新并重新编译"
echo "2. 如果节点不同步（last_masterchain_block_ago 很大），等待同步完成"
echo "3. 检查是否在正确的网络（主网/测试网）"