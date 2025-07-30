#!/bin/bash
# 修复 IPC 编译问题

set -e

echo "=== 修复 TON IPC 编译 ==="
echo

# 检查当前目录
if [ ! -f "CMakeLists.txt" ]; then
    echo "错误：请在 TON 源码根目录运行此脚本"
    exit 1
fi

echo "1. 清理旧的构建文件..."
rm -rf build/CMakeCache.txt build/CMakeFiles/

echo
echo "2. 创建新的构建配置..."
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DTON_IPC_ENABLED=ON

echo
echo "3. 验证 IPC 已启用..."
if grep -q "TON_IPC_ENABLED:BOOL=ON" CMakeCache.txt; then
    echo "✓ IPC 已在 CMake 中启用"
else
    echo "✗ IPC 未启用！检查 CMakeLists.txt"
    exit 1
fi

echo
echo "4. 编译 validator-engine..."
make -j$(nproc) validator-engine

echo
echo "5. 验证编译结果..."
if nm validator-engine/validator-engine | grep -q "IPCPublisher"; then
    echo "✓ IPC 符号已包含在二进制文件中"
    echo "找到以下 IPC 相关符号："
    nm validator-engine/validator-engine | grep -i "ipc" | head -10
else
    echo "✗ 编译失败：二进制文件中没有 IPC 符号"
    exit 1
fi

echo
echo "6. 检查文件大小（IPC 版本应该更大）..."
ls -lah validator-engine/validator-engine

echo
echo "=== 编译成功！==="
echo
echo "下一步："
echo "1. 停止服务: sudo systemctl stop ton"
echo "2. 备份原文件: sudo cp /usr/local/bin/validator-engine /usr/local/bin/validator-engine.backup"
echo "3. 安装新文件: sudo cp validator-engine/validator-engine /usr/local/bin/"
echo "4. 启动服务: sudo systemctl start ton"
echo "5. 检查日志: sudo journalctl -u ton -f | grep -i ipc"