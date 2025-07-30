#!/bin/bash
# 检查 TON 工作目录的日志

echo "=== 检查 /var/ton-work 日志 ==="
echo

# 查看 IPC 相关的所有日志
echo "1. 所有 IPC 相关日志："
sudo grep -h "IPC" /var/ton-work/log.thread*.log | tail -50

echo
echo "2. 查看 IPCPublisher 启动日志："
sudo grep -h "IPCPublisher::start_up\|Publisher started" /var/ton-work/log.thread*.log | tail -20

echo
echo "3. 查看区块相关日志："
sudo grep -h "process_block_broadcast\|new_block_broadcast\|block.*broadcast" /var/ton-work/log.thread*.log | tail -20

echo
echo "4. 查看最新的日志文件："
latest_log=$(ls -t /var/ton-work/log.thread*.log | head -1)
echo "最新日志文件: $latest_log"
echo "最后 50 行："
sudo tail -50 "$latest_log"

echo
echo "5. 检查 socket 文件："
ls -la /tmp/ton-ipc.sock

echo
echo "6. 检查是否有错误："
sudo grep -h "ERROR.*IPC\|Failed.*socket\|Failed.*bind" /var/ton-work/log.thread*.log | tail -20

echo
echo "7. 实时监控新日志："
echo "监控 IPC 和区块事件（按 Ctrl+C 停止）："
sudo tail -f /var/ton-work/log.thread*.log | grep -E "IPC|block.*broadcast|publish"