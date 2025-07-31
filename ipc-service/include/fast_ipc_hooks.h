/*
    Fast IPC Hooks for TON Node Integration
*/

#pragma once

#include "fast_ipc.h"
#include "td/utils/buffer.h"
#include <iostream>

namespace ton_ipc_hooks {

// Auto-initialize on first use
inline void ensureIPCStarted() {
    static bool initialized = false;
    if (!initialized) {
        if (ton_ipc::FastIPCService::getInstance().start()) {
            initialized = true;
            std::atexit([]() {
                ton_ipc::FastIPCService::getInstance().stop();
            });
        } else {
            std::cerr << "[Fast IPC] Failed to start IPC service" << std::endl;
        }
    }
}

// Hook for external messages
inline void hookExternalMessage(const std::string& source, 
                               const std::string& dest,
                               const td::BufferSlice& data,
                               const std::string& hash = "") {
    ensureIPCStarted();
    
    // Send raw data directly
    ton_ipc::FastIPCService::getInstance().sendExternalMessage(
        data.data(), data.size()
    );
}

// Hook for new blocks
inline void hookNewBlock(const std::string& block_id,
                        uint32_t gen_utime,
                        uint32_t delay_seconds,
                        uint32_t account_count,
                        uint32_t transaction_count,
                        const std::vector<std::string>& tx_hashes) {
    // For simple block notification without raw data
    // Not used in this implementation
}

// Hook for new blocks with raw data
inline void hookNewBlockWithData(const std::string& block_id,
                                uint32_t gen_utime,
                                uint32_t delay_seconds,
                                uint32_t account_count,
                                uint32_t transaction_count,
                                const std::vector<std::string>& tx_hashes,
                                const td::BufferSlice& raw_data) {
    ensureIPCStarted();
    
    // Send raw block data directly
    ton_ipc::FastIPCService::getInstance().sendNewBlock(
        raw_data.data(), raw_data.size()
    );
}

} // namespace ton_ipc_hooks