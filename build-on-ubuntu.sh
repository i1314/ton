#!/bin/bash
# Run this script on Ubuntu 22.04

set -e

# Install dependencies
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    clang \
    libssl-dev \
    libz-dev \
    libreadline-dev \
    libmicrohttpd-dev \
    pkg-config \
    libsecp256k1-dev \
    libsodium-dev \
    liblz4-dev \
    librocksdb-dev \
    ninja-build \
    git

# Clone or update the repository
if [ ! -d "ton" ]; then
    git clone https://github.com/ton-blockchain/ton.git
    cd ton
    git checkout custom  # Your branch with IPC changes
else
    cd ton
    git pull
fi

# Copy your IPC files (if not already in the branch)
# You'll need to transfer these files to the Ubuntu machine

# Build
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DTON_IPC_ENABLED=ON
make -j$(nproc) validator-engine

echo "Build complete!"
echo "Binary location: $(pwd)/validator-engine/validator-engine"