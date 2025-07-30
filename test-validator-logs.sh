#!/bin/bash
# 测试 validator 日志输出

echo "=== 测试 Validator 日志 ==="
echo

echo "1. 检查 validator-engine 版本和编译信息："
/usr/local/bin/validator-engine -V 2>&1 || echo "无法获取版本信息"

echo
echo "2. 检查二进制文件中的 IPC 字符串："
strings /usr/local/bin/validator-engine | grep -i "ipc\|publish" | head -20

echo
echo "3. 尝试手动运行 validator（只显示帮助）："
timeout 2 /usr/local/bin/validator-engine --help 2>&1 | grep -i "ipc\|log"

echo
echo "4. 检查 mytonctrl 的日志位置："
mytonctrl <<EOF 2>&1 | grep -i "log\|work"
status
exit
EOF

echo
echo "5. 查找所有可能的日志文件："
sudo find / -name "*.log" -path "*/ton*" -o -path "*/validator*" 2>/dev/null | grep -v "/proc" | head -20

echo
echo "6. 检查 systemd 服务配置："
sudo systemctl cat ton 2>/dev/null || sudo systemctl cat validator 2>/dev/null

echo
echo "7. 如果使用 mytonctrl，检查其日志设置："
if [ -f /usr/local/bin/mytoncore/mytoncore.py ]; then
    grep -i "log" /usr/local/bin/mytoncore/mytoncore.py | head -10
fi

echo
echo "=== 建议 ==="
echo "如果看不到任何 IPC 相关日志，可能是："
echo "1. 日志级别设置太高（只显示 ERROR）"
echo "2. 日志输出到了其他地方"
echo "3. 二进制文件确实没有包含 IPC 代码"
echo
echo "尝试设置详细日志级别："
echo "export VERBOSITY_LEVEL=4"
echo "或在启动参数中添加 -v 4"