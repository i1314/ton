/*
    TON IPC Subscription Service
    Minimal, high-performance IPC service for subscribing to blockchain events
*/

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

namespace ton_ipc {

// Message types
enum class MessageType : uint8_t {
    EXTERNAL_MESSAGE = 1,
    NEW_BLOCK = 2,
    SUBSCRIBE = 3,
    UNSUBSCRIBE = 4,
    HEARTBEAT = 5
};

// Subscription types
enum class SubscriptionType : uint32_t {
    EXTERNAL_MESSAGES = 1 << 0,
    NEW_BLOCKS = 1 << 1,
    ALL = EXTERNAL_MESSAGES | NEW_BLOCKS
};

// Message header (fixed size: 16 bytes)
struct MessageHeader {
    uint32_t magic = 0x544F4E30;  // "TON0"
    uint32_t version = 1;
    MessageType type;
    uint8_t reserved[3] = {0};
    uint32_t payload_size;
} __attribute__((packed));

// External message data
struct ExternalMessageData {
    uint64_t timestamp_ms;
    std::string source_addr;
    std::string dest_addr;
    std::vector<uint8_t> data;
    std::string hash;
};

// Block data  
struct BlockData {
    std::string block_id;
    uint32_t gen_utime;
    uint32_t delay_seconds;
    uint32_t account_count;
    uint32_t transaction_count;
    std::vector<std::string> transaction_hashes;  // First 5 transactions
    std::vector<uint8_t> raw_data;  // Optional: raw block data
};

class IPCService {
public:
    struct Config {
        std::string socket_path = "/tmp/ton-ipc.sock";
        size_t max_clients = 100;
        size_t max_queue_size = 10000;
        bool enable_compression = false;
        int worker_threads = 2;
    };

    IPCService(const Config& config = Config());
    ~IPCService();

    // Start/stop service
    bool start();
    void stop();

    // Hook functions to be called from TON node
    void onExternalMessage(const ExternalMessageData& msg);
    void onNewBlock(const BlockData& block);

    // Statistics
    struct Stats {
        std::atomic<uint64_t> messages_sent{0};
        std::atomic<uint64_t> blocks_sent{0};
        std::atomic<uint32_t> active_clients{0};
        std::atomic<uint64_t> dropped_messages{0};
    };
    
    const Stats& getStats() const { return stats_; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    Stats stats_;
};

// Singleton instance getter
IPCService& getIPCService();

// Hook macros for minimal code injection
#define TON_IPC_HOOK_EXTERNAL_MSG(source, dest, data, hash) \
    do { \
        try { \
            ton_ipc::ExternalMessageData msg; \
            msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>( \
                std::chrono::system_clock::now().time_since_epoch()).count(); \
            msg.source_addr = source; \
            msg.dest_addr = dest; \
            msg.data = data; \
            msg.hash = hash; \
            ton_ipc::getIPCService().onExternalMessage(msg); \
        } catch (...) {} \
    } while(0)

#define TON_IPC_HOOK_NEW_BLOCK(block_id_str, gen_utime, delay, accounts, txs, tx_hashes) \
    do { \
        try { \
            ton_ipc::BlockData block; \
            block.block_id = block_id_str; \
            block.gen_utime = gen_utime; \
            block.delay_seconds = delay; \
            block.account_count = accounts; \
            block.transaction_count = txs; \
            block.transaction_hashes = tx_hashes; \
            ton_ipc::getIPCService().onNewBlock(block); \
        } catch (...) {} \
    } while(0)

} // namespace ton_ipc