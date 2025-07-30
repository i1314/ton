# TON IPC 快速修复指南

## 问题原因
二进制文件中没有 IPC 符号，因为 CMake 默认没有启用 IPC。

## 修复步骤（在 Ubuntu 上执行）

```bash
# 1. 更新代码
cd ~/ton
git pull origin custom

# 2. 清理并重新配置（重要！）
cd build
rm -rf CMakeCache.txt CMakeFiles/
cmake .. -DCMAKE_BUILD_TYPE=Release -DTON_IPC_ENABLED=ON

# 3. 验证配置
grep "TON_IPC_ENABLED" CMakeCache.txt
# 应该显示: TON_IPC_ENABLED:BOOL=ON

# 4. 编译
make -j$(nproc) validator-engine

# 5. 验证符号
nm validator-engine/validator-engine | grep -i ipc
# 应该看到 IPCPublisher 相关符号

# 6. 安装
sudo systemctl stop ton
sudo cp validator-engine/validator-engine /usr/local/bin/
sudo systemctl start ton

# 7. 等待并检查
sleep 10
ls -la /tmp/ton-ipc.sock
sudo journalctl -u ton -n 50 | grep -i ipc

# 8. 测试
python3 ~/ton/validator/ipc-client-debug.py
```

## 如果还是不工作

运行完整的修复脚本：
```bash
chmod +x ~/ton/fix-ipc-build.sh
~/ton/fix-ipc-build.sh
```

## 验证检查清单

- [ ] CMakeCache.txt 中 TON_IPC_ENABLED=ON
- [ ] 二进制文件包含 IPCPublisher 符号
- [ ] /tmp/ton-ipc.sock 文件存在
- [ ] 日志中有 [IPC] 相关信息
- [ ] 客户端能接收到事件