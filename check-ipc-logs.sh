#!/bin/bash
# 检查 IPC 相关日志

echo "=== Checking IPC Logs ==="
echo

echo "1. Searching for IPC initialization logs:"
sudo journalctl -u ton --no-pager | grep -E "\[IPC\]|IPCPublisher" | tail -20

echo
echo "2. Searching for block-related logs:"
sudo journalctl -u ton --no-pager | grep -E "new_block|process_block" | tail -20

echo
echo "3. Checking if hooks are being called:"
echo "Recent logs with IPC tags:"
sudo journalctl -u ton --since "5 minutes ago" --no-pager | grep -E "\[IPC\]"

echo
echo "4. Full recent logs (last 50 lines):"
sudo journalctl -u ton --no-pager | tail -50

echo
echo "5. Check if binary has IPC symbols:"
if nm /usr/local/bin/validator-engine 2>/dev/null | grep -q "IPCPublisher"; then
    echo "✓ IPCPublisher symbols found in binary"
    nm /usr/local/bin/validator-engine | grep -i "publish" | head -10
else
    echo "✗ IPCPublisher symbols NOT found - may need recompilation"
fi