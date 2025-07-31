#!/bin/bash

# Build and test TON IPC Service

set -e

echo "Building TON IPC Service..."

# Create build directory
mkdir -p build
cd build

# Configure
cmake ..

# Build
make -j$(nproc)

echo "Build complete!"

# Run test server
echo "Starting test server..."
./test_server &
TEST_SERVER_PID=$!

# Wait for server to start
sleep 2

# Build and run Go client example
echo "Building Go client..."
cd ../client/go/example
go build -o ton_ipc_client

echo "Running Go client..."
./ton_ipc_client &
CLIENT_PID=$!

# Let it run for 30 seconds
echo "Running test for 30 seconds..."
sleep 30

# Stop processes
echo "Stopping test..."
kill $CLIENT_PID 2>/dev/null || true
kill $TEST_SERVER_PID 2>/dev/null || true

echo "Test complete!"