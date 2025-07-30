#!/bin/bash
# 测试 IPC 事件生成

echo "=== TON IPC 事件测试 ==="
echo

echo "IPC 已成功运行！现在需要等待事件："
echo
echo "1. 新区块事件："
echo "   - 主网约每 5 秒产生一个新区块"
echo "   - 测试网可能更慢"
echo "   - 你应该看到类似：[New Block] ID=... Seqno=..."
echo
echo "2. 外部消息事件："
echo "   - 当有人发送交易到网络时触发"
echo "   - 可以通过钱包发送测试交易"
echo
echo "3. 合约状态变化："
echo "   - 当合约执行并改变状态时触发"
echo "   - 需要有活跃的合约交易"
echo
echo "=== 生成测试事件 ==="
echo
echo "方法 1：等待自然事件（推荐）"
echo "   保持 ipc-client-example.py 运行，等待 1-2 分钟"
echo "   主网应该会有持续的新区块事件"
echo
echo "方法 2：发送测试交易"
echo "   使用 mytonctrl 发送小额测试交易："
echo "   mytonctrl"
echo "   > mg wallet_address 0.001"
echo
echo "方法 3：监控特定地址"
echo "   如果你想监控特定合约，可以修改 validator/ipc-publisher.cpp"
echo "   在 should_monitor_address 函数中添加地址过滤"
echo
echo "=== 调试建议 ==="
echo
echo "1. 确认节点同步状态："
echo "   mytonctrl"
echo "   > status"
echo "   确保显示 'synchronized'"
echo
echo "2. 检查 IPC 日志："
echo "   sudo journalctl -u ton -f | grep -i ipc"
echo
echo "3. 检查网络活动："
echo "   mytonctrl"
echo "   > vl  # 查看验证器列表"
echo "   > el  # 查看选举"
echo
echo "4. 运行增强版客户端（显示连接时间）："