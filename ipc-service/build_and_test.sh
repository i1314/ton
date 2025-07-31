#!/bin/bash

set -e

echo "Building Fast IPC Service..."

# Create build directory
mkdir -p build
cd build

# Configure
cmake ..

# Build
make -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)

echo "Build complete!"
echo ""
echo "Starting test server..."
./test_server 1000 1024 &
SERVER_PID=$!

sleep 2

echo ""
echo "Starting Go client..."
cd ../clients/go
go run test_client.go &
CLIENT_PID=$!

echo ""
echo "Running for 30 seconds..."
sleep 30

echo ""
echo "Stopping processes..."
kill $CLIENT_PID 2>/dev/null || true
kill $SERVER_PID 2>/dev/null || true

echo "Test complete!"