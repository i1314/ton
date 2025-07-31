/*
    High-Performance IPC Service for TON MEV
    Using Unix Domain Sockets with nanosecond precision
*/

#pragma once

#include <string>
#include <atomic>
#include <cstdint>
#include <vector>
#include <mutex>
#include <memory>
#include <sys/socket.h>
#include <sys/un.h>

namespace ton_ipc {

// 16-byte message header (packed)
struct FastMessage {
    uint64_t timestamp_ns;  // Nanosecond timestamp
    uint32_t data_length;   // Payload size
    uint8_t  reserved[4];   // Alignment/Future use
} __attribute__((packed));

static_assert(sizeof(FastMessage) == 16, "FastMessage must be 16 bytes");

// IPC channel type
enum class ChannelType {
    EXTERNAL_MESSAGE,
    NEW_BLOCK
};

// Single IPC channel (one UDS)
class IPCChannel {
public:
    IPCChannel(const std::string& socket_path, ChannelType type);
    ~IPCChannel();
    
    // Initialize and start listening
    bool start();
    void stop();
    
    // Send data to all connected clients
    void broadcast(const uint8_t* data, size_t length);
    
    // Get statistics
    uint64_t getMessagesSent() const { return messages_sent_.load(); }
    uint64_t getBytessSent() const { return bytes_sent_.load(); }
    uint64_t getClientsConnected() const { return clients_connected_.load(); }
    
private:
    std::string socket_path_;
    ChannelType channel_type_;
    int server_fd_ = -1;
    std::atomic<bool> running_{false};
    
    // Statistics
    std::atomic<uint64_t> messages_sent_{0};
    std::atomic<uint64_t> bytes_sent_{0};
    std::atomic<uint64_t> clients_connected_{0};
    
    // Accept clients in background
    void acceptLoop();
    
    // Client connection info
    struct Client {
        int fd;
        uint64_t connected_at_ns;
    };
    std::vector<Client> clients_;
    std::mutex clients_mutex_;
    
    // High-resolution timestamp
    uint64_t getNanoTimestamp() const;
    
    // Remove disconnected client
    void removeClient(int fd);
};

// Main IPC service with two channels
class FastIPCService {
public:
    static FastIPCService& getInstance() {
        static FastIPCService instance;
        return instance;
    }
    
    // Start both channels
    bool start();
    void stop();
    
    // Send external message
    void sendExternalMessage(const uint8_t* data, size_t length);
    
    // Send new block
    void sendNewBlock(const uint8_t* data, size_t length);
    
    // Get channels for stats
    IPCChannel* getExternalMessageChannel() { return extmsg_channel_.get(); }
    IPCChannel* getNewBlockChannel() { return blocks_channel_.get(); }
    
private:
    FastIPCService() = default;
    ~FastIPCService() { stop(); }
    
    std::unique_ptr<IPCChannel> extmsg_channel_;
    std::unique_ptr<IPCChannel> blocks_channel_;
};

} // namespace ton_ipc