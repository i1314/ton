# Ubuntu 22.04 TON Compilation Fix

## Prerequisites

```bash
# Update system
sudo apt update && sudo apt upgrade -y

# Install required dependencies
sudo apt install -y build-essential cmake clang-14 ninja-build \
    libssl-dev libmicrohttpd-dev libreadline-dev \
    liblz4-dev libzlib1g-dev libsodium-dev libblas-dev \
    pkg-config autoconf automake libtool git

# Install LLVM 14 (recommended for Ubuntu 22.04)
sudo apt install -y llvm-14 clang-14 libc++-14-dev libc++abi-14-dev

# Set clang-14 as default
sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-14 100
sudo update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-14 100
```

## Common Issues and Solutions

### 1. Block-auto.cpp Generation Failure

If you see:
```
FAILED: /ton/crypto/block/block-auto.cpp /ton/crypto/block/block-auto.h 
Segmentation fault: 11  /ton/build/crypto/tlbc -o block-auto -n block::gen -z block.tlb
```

**Solution**: Use pre-generated files
```bash
# Download pre-generated files
cd crypto/block/
wget https://github.com/ton-blockchain/ton/raw/master/crypto/block/block-auto.cpp
wget https://github.com/ton-blockchain/ton/raw/master/crypto/block/block-auto.h
wget https://github.com/ton-blockchain/ton/raw/master/crypto/block/block-auto-fwd.h
cd ../..
```

### 2. Compiler Version Issues

If you get C++ standard errors:
```bash
# Use specific compiler versions
export CC=/usr/bin/clang-14
export CXX=/usr/bin/clang++-14
```

### 3. Clean Build Process

```bash
# Clean build directory
rm -rf build
mkdir build && cd build

# Configure with proper flags
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=/usr/bin/clang-14 \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++-14 \
    -DCMAKE_CXX_FLAGS="-stdlib=libc++ -O2" \
    -GNinja

# Build with limited parallelism to avoid memory issues
ninja -j4
```

### 4. Memory Issues

If build fails due to memory:
```bash
# Create swap if needed
sudo fallocate -l 8G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile

# Build with fewer jobs
ninja -j2
```

### 5. Full-node.cpp Specific Fix

The current full-node.cpp uses newer block parsing methods. If compilation fails:

```bash
# Revert to simpler logging temporarily
cd .. # to ton root
git checkout validator/full-node.cpp
```

Or apply this minimal patch for testing:
```cpp
void FullNodeImpl::process_block_broadcast(BlockBroadcast broadcast) {
  LOG(ERROR) << "BLOCK DISCOVERED: " << broadcast.block_id.to_str() 
             << " catchain_seqno=" << broadcast.catchain_seqno
             << " validator_set_hash=" << broadcast.validator_set_hash
             << " data_size=" << broadcast.data.size()
             << " signature_count=" << broadcast.signatures.size();
             
  // Comment out advanced parsing temporarily
  /*
  auto block_res = create_block(broadcast.block_id, broadcast.data.clone());
  if (block_res.is_ok()) {
    // Advanced parsing code...
  }
  */
  
  send_block_broadcast_to_custom_overlays(broadcast);
  td::actor::send_closure(validator_manager_, &ValidatorManagerInterface::new_block_broadcast, std::move(broadcast),
                          [](td::Result<td::Unit> R) {
                            if (R.is_error()) {
                              if (R.error().code() == ErrorCode::notready) {
                                LOG(DEBUG) << "dropped broadcast: " << R.move_as_error();
                              } else {
                                LOG(INFO) << "dropped broadcast: " << R.move_as_error();
                              }
                            }
                          });
}
```

## Docker Alternative

If native compilation continues to fail:
```bash
# Use official Docker image
docker pull ubuntu:22.04

# Run with source mounted
docker run -it -v $(pwd):/ton -w /ton ubuntu:22.04 bash

# Inside container, install deps and build
apt update && apt install -y build-essential cmake ninja-build \
    libssl-dev libmicrohttpd-dev libreadline-dev \
    liblz4-dev zlib1g-dev libsodium-dev git

cd /ton
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -GNinja
ninja -j4
```

## Test Compilation

To test if basic compilation works:
```bash
# Build only essential components first
ninja ton_crypto
ninja tdutils
ninja validator-engine
```