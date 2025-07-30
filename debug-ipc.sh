#!/bin/bash
# IPC 调试脚本

echo "=== TON IPC Debug Script ==="
echo

# 1. 检查进程
echo "1. Checking validator-engine process:"
if pgrep -f validator-engine > /dev/null; then
    echo "✓ validator-engine is running"
    echo "Process info:"
    ps aux | grep validator-engine | grep -v grep
else
    echo "✗ validator-engine is not running"
fi
echo

# 2. 检查二进制文件
echo "2. Checking binary for IPC support:"
VALIDATOR_BIN=$(which validator-engine || echo "/usr/local/bin/validator-engine")
if [ -f "$VALIDATOR_BIN" ]; then
    echo "Binary found at: $VALIDATOR_BIN"
    if strings "$VALIDATOR_BIN" | grep -q "ton-ipc.sock"; then
        echo "✓ IPC code is present in binary"
    else
        echo "✗ IPC code NOT found in binary - may not be compiled with -DTON_IPC_ENABLED=ON"
    fi
    
    # 检查文件信息
    echo "File info:"
    ls -la "$VALIDATOR_BIN"
    file "$VALIDATOR_BIN"
else
    echo "✗ validator-engine binary not found"
fi
echo

# 3. 检查 socket 文件
echo "3. Checking IPC socket:"
if [ -S /tmp/ton-ipc.sock ]; then
    echo "✓ Socket exists"
    ls -la /tmp/ton-ipc.sock
else
    echo "✗ Socket does not exist at /tmp/ton-ipc.sock"
    echo "Checking /tmp directory:"
    ls -la /tmp/ | grep -E "(ton|ipc|sock)"
fi
echo

# 4. 检查日志
echo "4. Checking logs for IPC messages:"
echo "System logs (last 20 lines):"
sudo journalctl -u ton --no-pager | tail -20 | grep -E "(IPC|ipc|socket)" || echo "No IPC messages in system logs"
echo
echo "Validator logs:"
if [ -f /var/log/ton/validator.log ]; then
    tail -20 /var/log/ton/validator.log | grep -E "(IPC|ipc|socket)" || echo "No IPC messages in validator log"
else
    echo "Validator log not found at /var/log/ton/validator.log"
fi
echo

# 5. 检查权限
echo "5. Checking permissions:"
echo "Current user: $(whoami)"
echo "Validator user: $(ps aux | grep validator-engine | grep -v grep | awk '{print $1}' | head -1)"
echo "/tmp permissions:"
ls -ld /tmp
echo

# 6. 手动运行测试
echo "6. Testing manual run:"
echo "Try running manually to see errors:"
echo "sudo -u validator $VALIDATOR_BIN --help 2>&1 | grep -i ipc"
echo

# 7. 检查编译标志
echo "7. Checking if binary was compiled with IPC:"
if nm "$VALIDATOR_BIN" 2>/dev/null | grep -q "IPCPublisher"; then
    echo "✓ IPCPublisher symbols found"
else
    echo "✗ IPCPublisher symbols NOT found - not compiled with IPC"
fi
echo

echo "=== Recommendations ==="
echo "1. If IPC code is not in binary, recompile with -DTON_IPC_ENABLED=ON"
echo "2. Check validator startup logs for IPC initialization errors"
echo "3. Try running validator manually to see startup messages"
echo "4. Ensure the validator process has permission to create sockets in /tmp"