# Ubuntu 22.04 编译指南 - TON with IPC

## 在 Ubuntu 22.04 上编译步骤

### 1. 安装依赖

```bash
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
    ninja-build \
    git \
    ccache
```

### 2. 安装 RocksDB（如果系统包太旧）

```bash
# 方式1：使用系统包
sudo apt-get install -y librocksdb-dev

# 方式2：从源码编译（如果系统包版本太旧）
git clone https://github.com/facebook/rocksdb.git
cd rocksdb
git checkout v6.29.5
make -j$(nproc) static_lib
sudo make install-static
cd ..
```

### 3. 克隆代码

```bash
git clone git@github.com:i1314/ton.git
cd ton
git checkout custom
git submodule update --init --recursive
```

### 4. 编译

```bash
# 创建构建目录
mkdir build
cd build

# 配置（启用 IPC）
cmake .. -DCMAKE_BUILD_TYPE=Release -DTON_IPC_ENABLED=ON

# 或者使用 clang 编译器（推荐）
CC=clang CXX=clang++ cmake .. -DCMAKE_BUILD_TYPE=Release -DTON_IPC_ENABLED=ON

# 编译
make -j$(nproc) validator-engine
```

### 5. 验证编译结果

```bash
# 检查二进制文件
ls -la validator-engine/validator-engine
file validator-engine/validator-engine

# 检查 IPC 功能是否编译进去
strings validator-engine/validator-engine | grep -i ipc
```

### 6. 运行测试

```bash
# 复制测试客户端
cp ../validator/ipc-client-example.py .

# 运行 validator-engine（需要配置文件）
# ./validator-engine/validator-engine [你的参数]

# 在另一个终端测试 IPC
python3 ipc-client-example.py
```

## 常见问题

### 1. 依赖问题
如果遇到依赖版本问题，可以尝试：
```bash
# 使用 Ubuntu 22.04 的 backports
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt-get update
```

### 2. 编译错误
如果遇到编译错误，尝试：
```bash
# 清理并重新配置
rm -rf CMakeCache.txt CMakeFiles/
cmake .. -DCMAKE_BUILD_TYPE=Release -DTON_IPC_ENABLED=ON
```

### 3. 内存不足
如果编译时内存不足：
```bash
# 减少并行编译数
make -j2 validator-engine
```

## IPC 功能说明

编译完成后，validator-engine 将具有以下 IPC 功能：

1. **Unix Socket**: `/tmp/ton-ipc.sock`
2. **数据格式**: 管道分隔的文本格式
   - 外部消息: `1|hash|destination|workchain|address|size|timestamp`
   - 新区块: `2|block_id|seqno|workchain|shard|timestamp|prev_block_id`
   - 合约状态: `3|address|workchain|block_id|lt|old_hash|new_hash|timestamp`

## 静态编译（可选）

如果需要在其他系统上运行，可以静态编译：
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DTON_IPC_ENABLED=ON -DTON_STATIC=ON
make -j$(nproc) validator-engine
```