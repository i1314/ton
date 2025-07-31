#!/bin/bash

echo "End-to-End IPC Test"
echo "==================="
echo ""

# Kill any existing servers
killall test_server 2>/dev/null || true

# Start server
echo "Starting test server..."
./build/test_server 50 1024 &
SERVER_PID=$!
sleep 1

# Run Go client for a short time
echo "Starting Go client..."
cd clients/go
go run test/test_client.go &
CLIENT_PID=$!

# Let it run for 2 seconds
sleep 2

# Kill client
echo ""
echo "Stopping client..."
kill -INT $CLIENT_PID 2>/dev/null
sleep 0.5

# Kill server
echo "Stopping server..."
kill -INT $SERVER_PID 2>/dev/null
sleep 0.5

# Final cleanup
killall test_server 2>/dev/null || true
killall test_client 2>/dev/null || true

echo ""
echo "Test completed!"