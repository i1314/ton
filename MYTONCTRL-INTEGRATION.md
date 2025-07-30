# MyTonCtrl IPC 集成方案

## 最佳方案：修改 mytonctrl 源码

由于 mytonctrl 有自己的编译系统（`upgrade` 命令），最好的方式是修改它使用的源码。

### 步骤 1：找到 mytonctrl 的 TON 源码位置

```bash
# 查找 mytonctrl 的安装位置
sudo find / -name "mytoncore" -type d 2>/dev/null

# 通常在这些位置之一：
# /usr/src/mytoncore/
# /opt/mytoncore/
# ~/.local/share/mytoncore/
# /usr/local/bin/mytoncore/

# 查看源码位置
ls -la /usr/src/mytoncore/ton/
# 或
ls -la ~/.local/share/mytoncore/ton/
```

### 步骤 2：替换源码

```bash
# 假设源码在 /usr/src/mytoncore/ton
cd /usr/src/mytoncore/ton

# 添加你的远程仓库
git remote add ipc-version https://github.com/i1314/ton.git

# 获取 IPC 分支
git fetch ipc-version custom

# 切换到 IPC 分支
git checkout ipc-version/custom

# 或者如果你想保留原始版本，创建新分支
git checkout -b custom-ipc ipc-version/custom
```

### 步骤 3：使用 mytonctrl 重新编译

```bash
# 进入 mytonctrl
mytonctrl

# 运行升级命令，这会重新编译 TON 组件
> upgrade

# 等待编译完成
> status

# 退出
> exit
```

### 步骤 4：验证 IPC 功能

```bash
# 检查是否启用了 IPC
ls -la /tmp/ton-ipc.sock

# 运行测试客户端
python3 /usr/src/mytoncore/ton/validator/ipc-client-example.py
```

## 备选方案：手动集成

如果 mytonctrl 的 upgrade 命令没有使用正确的 CMake 选项，你需要：

### 1. 找到 mytonctrl 的编译脚本

```bash
# 查找编译脚本
find /usr -name "mytoninstaller.py" -o -name "mytoncore.py" 2>/dev/null
grep -r "cmake" /usr/local/bin/mytoncore/ 2>/dev/null
```

### 2. 修改编译选项

找到 CMake 命令并添加 `-DTON_IPC_ENABLED=ON`：

```python
# 原始命令可能类似：
cmake_cmd = "cmake .. -DCMAKE_BUILD_TYPE=Release"

# 修改为：
cmake_cmd = "cmake .. -DCMAKE_BUILD_TYPE=Release -DTON_IPC_ENABLED=ON"
```

### 3. 或者创建包装脚本

```bash
#!/bin/bash
# 保存为 /usr/local/bin/upgrade-with-ipc.sh

# 找到 mytoncore 目录
MYTONCORE_DIR=$(find /usr -name "mytoncore" -type d | head -1)
TON_SRC="$MYTONCORE_DIR/ton"

if [ ! -d "$TON_SRC" ]; then
    echo "TON source not found!"
    exit 1
fi

cd "$TON_SRC"

# 更新到 IPC 版本
git fetch
git checkout custom

# 编译
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DTON_IPC_ENABLED=ON
make -j$(nproc) validator-engine

# 安装
sudo systemctl stop ton
sudo cp validator-engine/validator-engine /usr/local/bin/
sudo systemctl start ton

echo "IPC version installed successfully!"
```

## 直接替换方案（最简单但需要注意版本）

如果你确定版本兼容：

```bash
# 1. 停止服务
mytonctrl
> stop
> exit

# 2. 替换文件
sudo cp /path/to/your/validator-engine /usr/local/bin/validator-engine

# 3. 启动服务
mytonctrl
> start
> status
> exit
```

## 验证脚本

```bash
#!/bin/bash
# 保存为 check-ipc.sh

echo "=== MyTonCtrl IPC Integration Check ==="

# 1. 检查进程
if pgrep -f validator-engine > /dev/null; then
    echo "✓ Validator is running"
else
    echo "✗ Validator is not running"
fi

# 2. 检查 IPC socket
if [ -S /tmp/ton-ipc.sock ]; then
    echo "✓ IPC socket exists"
    ls -la /tmp/ton-ipc.sock
else
    echo "✗ IPC socket not found"
fi

# 3. 检查二进制文件
if strings /usr/local/bin/validator-engine | grep -q "ton-ipc.sock"; then
    echo "✓ IPC code is present in binary"
else
    echo "✗ IPC code not found in binary"
fi

# 4. 检查日志
echo -e "\nRecent logs:"
sudo journalctl -u ton --no-pager | tail -10
```

## 注意事项

1. **版本兼容性**：确保 IPC 修改基于 mytonctrl 使用的相同 TON 版本
2. **备份**：始终备份原始文件
3. **测试**：先在测试环境验证
4. **监控**：替换后密切监控日志和性能