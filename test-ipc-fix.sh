#!/bin/bash
# 测试 IPC 修复

echo "=== IPC Fix Test Script ==="
echo
echo "请在 Ubuntu 上运行以下命令来测试修复："
echo
echo "1. 更新代码："
echo "   cd ~/ton"
echo "   git pull"
echo "   git checkout custom"
echo
echo "2. 重新编译："
echo "   cd build"
echo "   make -j\$(nproc) validator-engine"
echo
echo "3. 替换二进制文件："
echo "   sudo systemctl stop ton"
echo "   sudo cp validator-engine/validator-engine /usr/local/bin/validator-engine"
echo "   sudo systemctl start ton"
echo
echo "4. 等待几秒后检查："
echo "   ls -la /tmp/ton-ipc.sock"
echo "   python3 ~/ton/validator/ipc-client-example.py"
echo
echo "如果还是没有 socket，检查日志："
echo "   sudo journalctl -u ton -f | grep -i ipc"
echo
echo "或手动运行查看错误："
echo "   sudo -u validator /usr/local/bin/validator-engine --help"