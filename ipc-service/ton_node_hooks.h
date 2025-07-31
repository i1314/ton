/*
    TON Node IPC Hooks
    Minimal injection points for TON validator
*/

#pragma once

#include "ipc_service.h"
#include <chrono>
#include <vector>
#include <string>
#include <cstdlib>
#include "td/utils/buffer.h"

namespace ton_ipc_hooks {

// Auto-initialize on first use
class AutoInit {
public:
    AutoInit() {
        static bool initialized = false;
        if (!initialized) {
            ton_ipc::IPCService::Config config;
            config.socket_path = "/tmp/ton-ipc.sock";
            config.max_clients = 100;
            config.max_queue_size = 10000;
            config.worker_threads = 2;
            
            if (ton_ipc::getIPCService().start()) {
                initialized = true;
                // Register cleanup on exit
                std::atexit([]() {
                    ton_ipc::getIPCService().stop();
                });
            }
        }
    }
};

// Initialize IPC service (optional - service auto-starts on first use)
inline bool initializeIPCService(const std::string& socket_path = "/tmp/ton-ipc.sock") {
    static AutoInit auto_init;
    return true;
}

// Shutdown IPC service (call at node shutdown)
inline void shutdownIPCService() {
    ton_ipc::getIPCService().stop();
}

// Hook for external messages in full-node-shard.cpp
inline void hookExternalMessage(const std::string& source, 
                               const std::string& dest,
                               const td::BufferSlice& data,
                               const std::string& hash = "") {
    static AutoInit auto_init;  // Ensure service is started
    
    ton_ipc::ExternalMessageData msg;
    msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    msg.source_addr = source;
    msg.dest_addr = dest;
    msg.data = std::vector<uint8_t>(data.data(), data.data() + data.size());
    msg.hash = hash;
    
    ton_ipc::getIPCService().onExternalMessage(msg);
}

// Hook for new blocks in full-node.cpp
inline void hookNewBlock(const std::string& block_id,
                        uint32_t gen_utime,
                        uint32_t delay_seconds,
                        uint32_t account_count,
                        uint32_t transaction_count,
                        const std::vector<std::string>& tx_hashes) {
    ton_ipc::BlockData block;
    block.block_id = block_id;
    block.gen_utime = gen_utime;
    block.delay_seconds = delay_seconds;
    block.account_count = account_count;
    block.transaction_count = transaction_count;
    block.transaction_hashes = tx_hashes;
    block.has_raw_data = false;
    
    ton_ipc::getIPCService().onNewBlock(block);
}

// Hook for new blocks with full data
inline void hookNewBlockWithData(const std::string& block_id,
                                uint32_t gen_utime,
                                uint32_t delay_seconds,
                                uint32_t account_count,
                                uint32_t transaction_count,
                                const std::vector<std::string>& tx_hashes,
                                const td::BufferSlice& raw_data) {
    static AutoInit auto_init;  // Ensure service is started
    
    ton_ipc::BlockData block;
    block.block_id = block_id;
    block.gen_utime = gen_utime;
    block.delay_seconds = delay_seconds;
    block.account_count = account_count;
    block.transaction_count = transaction_count;
    block.transaction_hashes = tx_hashes;
    block.raw_block_data = std::vector<uint8_t>(raw_data.data(), raw_data.data() + raw_data.size());
    block.has_raw_data = true;
    
    ton_ipc::getIPCService().onNewBlock(block);
}

} // namespace ton_ipc_hooks