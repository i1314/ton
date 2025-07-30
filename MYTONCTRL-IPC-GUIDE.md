# MyTonCtrl 集成 IPC 版本指南

## 方法1：直接替换（推荐）

### 1. 备份原始文件
```bash
# 找到 mytonctrl 的 validator-engine 位置
sudo find /usr -name "validator-engine" -type f 2>/dev/null

# 通常在这些位置之一：
# /usr/local/bin/validator-engine
# /usr/bin/ton/validator-engine
# /opt/ton/validator-engine

# 备份原始文件
sudo cp /usr/local/bin/validator-engine /usr/local/bin/validator-engine.backup
```

### 2. 替换为 IPC 版本
```bash
# 假设你已经编译好了 IPC 版本
sudo cp ./build/validator-engine/validator-engine /usr/local/bin/validator-engine
sudo chmod +x /usr/local/bin/validator-engine
```

### 3. 重启服务
```bash
# 使用 mytonctrl 重启
mytonctrl
> status  # 查看当前状态
> stop    # 停止节点
> start   # 启动节点
> exit

# 或者使用 systemctl
sudo systemctl restart ton
# 或
sudo systemctl restart validator
```

### 4. 验证 IPC 功能
```bash
# 检查 socket 是否创建
ls -la /tmp/ton-ipc.sock

# 运行测试客户端
python3 /path/to/ipc-client-example.py
```

## 方法2：修改 mytonctrl 配置

### 1. 找到 mytonctrl 配置
```bash
# 配置文件通常在
cat /usr/local/bin/mytonctrl/mytonctrl.py
# 或
cat ~/.local/share/mytoncore/config.json
```

### 2. 修改 validator-engine 路径
编辑配置文件，将 validator-engine 路径指向你的 IPC 版本。

## 方法3：使用符号链接
```bash
# 备份原始文件
sudo mv /usr/local/bin/validator-engine /usr/local/bin/validator-engine.original

# 创建符号链接
sudo ln -s /path/to/your/build/validator-engine/validator-engine /usr/local/bin/validator-engine
```

## 注意事项

### 1. 版本兼容性
确保你的 IPC 版本基于与 mytonctrl 相同的 TON 版本：
```bash
# 检查当前版本
validator-engine -V
```

### 2. 权限问题
```bash
# 确保权限正确
sudo chown root:root /usr/local/bin/validator-engine
sudo chmod 755 /usr/local/bin/validator-engine
```

### 3. SELinux/AppArmor
如果系统启用了 SELinux 或 AppArmor，可能需要更新策略：
```bash
# For SELinux
sudo restorecon /usr/local/bin/validator-engine

# For AppArmor
sudo aa-complain /usr/local/bin/validator-engine
```

### 4. 日志检查
```bash
# 查看 validator 日志
sudo journalctl -u ton -f
# 或
tail -f /var/log/ton/validator.log
```

## IPC 配置

### 1. 环境变量
可以通过环境变量配置 IPC：
```bash
# 编辑 systemd 服务文件
sudo systemctl edit ton

# 添加环境变量
[Service]
Environment="TON_IPC_SOCKET=/tmp/ton-ipc.sock"
Environment="TON_IPC_MAX_CLIENTS=10"
```

### 2. 监控地址配置
创建配置文件：
```bash
sudo mkdir -p /etc/ton
sudo cat > /etc/ton/ipc-config.json << EOF
{
  "monitored_addresses": [
    "EQD2NmD_lH5f5u1Kj3KfGyTvhZSX0Eg6qp2a5IQUKXxOG21n",
    "EQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAM9c"
  ]
}
EOF
```

## 测试脚本

创建一个测试脚本来验证 IPC 功能：

```bash
#!/bin/bash
# save as test-ipc.sh

echo "Checking IPC functionality..."

# Check if socket exists
if [ -S /tmp/ton-ipc.sock ]; then
    echo "✓ IPC socket found"
else
    echo "✗ IPC socket not found"
    exit 1
fi

# Check if validator is running
if pgrep -x "validator-engine" > /dev/null; then
    echo "✓ Validator-engine is running"
else
    echo "✗ Validator-engine is not running"
    exit 1
fi

# Try to connect
timeout 5 python3 -c "
import socket
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
try:
    s.connect('/tmp/ton-ipc.sock')
    print('✓ Successfully connected to IPC socket')
    s.close()
except Exception as e:
    print(f'✗ Failed to connect: {e}')
"
```

## 故障排除

### 1. Socket 权限问题
```bash
# 检查 socket 权限
ls -la /tmp/ton-ipc.sock

# 如果需要，调整权限
sudo chmod 666 /tmp/ton-ipc.sock
```

### 2. 进程未启动
```bash
# 手动运行查看错误
sudo -u validator /usr/local/bin/validator-engine --help
```

### 3. 恢复原始版本
```bash
# 如果出现问题，恢复备份
sudo cp /usr/local/bin/validator-engine.backup /usr/local/bin/validator-engine
sudo systemctl restart ton
```

## 推荐步骤总结

1. **先在测试环境验证**
2. **备份原始 validator-engine**
3. **直接替换二进制文件**
4. **重启服务**
5. **验证 IPC 功能**
6. **监控日志确保正常运行**