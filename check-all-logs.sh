#!/bin/bash
# 全面检查日志

echo "=== 全面日志检查 ==="
echo

echo "1. 检查进程是否在运行："
ps aux | grep validator-engine | grep -v grep

echo
echo "2. 检查最近的所有日志（包括 INFO 级别）："
sudo journalctl -u ton --since "10 minutes ago" --no-pager | tail -100

echo
echo "3. 搜索所有可能的日志位置："
echo "系统日志："
sudo grep -i "ipc\|publish" /var/log/syslog 2>/dev/null | tail -20

echo
echo "4. 检查 TON 特定日志文件："
if [ -d /var/ton-work ]; then
    echo "TON 工作目录日志："
    find /var/ton-work -name "*.log" -type f -exec echo "=== {} ===" \; -exec tail -20 {} \; 2>/dev/null
fi

if [ -d /var/log/ton ]; then
    echo "TON 日志目录："
    find /var/log/ton -name "*.log" -type f -exec echo "=== {} ===" \; -exec tail -20 {} \; 2>/dev/null
fi

echo
echo "5. 实时监控日志（运行后等待新区块）："
echo "按 Ctrl+C 停止"
echo "---"
sudo journalctl -u ton -f --no-pager | grep -E "block|broadcast|IPC|publish|external"