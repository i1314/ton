/*
    TON IPC Subscription Service Implementation
*/

#include "ipc_service.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <chrono>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <errno.h>

namespace ton_ipc {

class IPCService::Impl {
public:
    explicit Impl(const Config& config) : config_(config) {
        // Ignore SIGPIPE to prevent crashes on broken connections
        signal(SIGPIPE, SIG_IGN);
    }

    ~Impl() {
        stop();
    }

    bool start() {
        if (running_.exchange(true)) {
            std::cerr << "[IPC] Service already running" << std::endl;
            return false; // Already running
        }

        std::cerr << "[IPC] Starting IPC service on " << config_.socket_path << std::endl;

        // Create Unix domain socket
        server_fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (server_fd_ < 0) {
            std::cerr << "[IPC] Failed to create socket: " << strerror(errno) << std::endl;
            running_ = false;
            return false;
        }

        // Remove existing socket file
        unlink(config_.socket_path.c_str());

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, config_.socket_path.c_str(), sizeof(addr.sun_path) - 1);

        if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(server_fd_);
            running_ = false;
            return false;
        }

        if (listen(server_fd_, 128) < 0) {
            close(server_fd_);
            unlink(config_.socket_path.c_str());
            running_ = false;
            return false;
        }

        // Start worker threads
        for (int i = 0; i < config_.worker_threads; ++i) {
            workers_.emplace_back(&Impl::workerLoop, this);
        }

        // Start acceptor thread
        acceptor_thread_ = std::thread(&Impl::acceptorLoop, this);

        std::cerr << "[IPC] Service started successfully with " << config_.worker_threads 
                  << " worker threads" << std::endl;
        return true;
    }

    void stop() {
        if (!running_.exchange(false)) {
            return;
        }

        // Close server socket
        if (server_fd_ >= 0) {
            close(server_fd_);
            server_fd_ = -1;
        }

        // Wake up workers
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            queue_cv_.notify_all();
        }

        // Wait for threads
        if (acceptor_thread_.joinable()) {
            acceptor_thread_.join();
        }
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        // Close all client connections
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            for (auto& client : clients_) {
                close(client.fd);
            }
            clients_.clear();
        }

        // Clean up socket file
        unlink(config_.socket_path.c_str());
    }

    void onExternalMessage(const ExternalMessageData& msg, Stats& stats) {
        if (!running_) {
            std::cerr << "[IPC] Service not running, dropping external message" << std::endl;
            return;
        }

        std::cerr << "[IPC] External message received: from=" << msg.source_addr 
                  << " to=" << msg.dest_addr 
                  << " size=" << msg.data.size() << std::endl;
        
        auto data = serializeExternalMessage(msg);
        enqueueMessage(MessageType::EXTERNAL_MESSAGE, std::move(data), stats);
        stats.messages_sent++;
    }

    void onNewBlock(const BlockData& block, Stats& stats) {
        if (!running_) {
            std::cerr << "[IPC] Service not running, dropping block" << std::endl;
            return;
        }

        std::cerr << "[IPC] New block received: id=" << block.block_id 
                  << " accounts=" << block.account_count
                  << " txs=" << block.transaction_count << std::endl;
        
        auto data = serializeBlock(block);
        enqueueMessage(MessageType::NEW_BLOCK, std::move(data), stats);
        stats.blocks_sent++;
    }

private:
    struct Client {
        int fd;
        uint32_t subscriptions = 0;
        std::chrono::steady_clock::time_point last_activity;
    };

    struct QueueItem {
        MessageType type;
        std::vector<uint8_t> data;
    };

    void acceptorLoop() {
        while (running_) {
            struct pollfd pfd;
            pfd.fd = server_fd_;
            pfd.events = POLLIN;
            pfd.revents = 0;

            int ret = poll(&pfd, 1, 100); // 100ms timeout
            if (ret <= 0) continue;

            struct sockaddr_un client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept4(server_fd_, (struct sockaddr*)&client_addr, 
                                   &client_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
            
            if (client_fd < 0) continue;

            // Add client
            {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                if (clients_.size() >= config_.max_clients) {
                    std::cerr << "[IPC] Max clients reached, rejecting connection" << std::endl;
                    close(client_fd);
                    continue;
                }
                
                Client client;
                client.fd = client_fd;
                client.subscriptions = 0;
                client.last_activity = std::chrono::steady_clock::now();
                clients_.push_back(client);
                
                std::cerr << "[IPC] Client connected, fd=" << client_fd 
                          << ", total clients=" << clients_.size() << std::endl;
                
                // Start client handler thread
                std::thread(&Impl::handleClient, this, client_fd).detach();
            }
        }
    }

    void handleClient(int client_fd) {
        std::cerr << "[IPC] Client handler started for fd=" << client_fd << std::endl;
        
        while (running_) {
            MessageHeader header;
            ssize_t n = recv(client_fd, &header, sizeof(header), MSG_WAITALL);
            
            if (n != sizeof(header)) {
                if (n == 0) {
                    std::cerr << "[IPC] Client disconnected, fd=" << client_fd << std::endl;
                } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    std::cerr << "[IPC] Client read error, fd=" << client_fd << ": " << strerror(errno) << std::endl;
                }
                break;
            }
            
            // Validate header
            if (header.magic != 0x544F4E30 || header.version != 1) {
                std::cerr << "[IPC] Invalid header from client fd=" << client_fd << std::endl;
                break;
            }
            
            // Handle subscription
            if (header.type == MessageType::SUBSCRIBE) {
                if (header.payload_size == 4) {
                    uint32_t subscription_type;
                    if (recv(client_fd, &subscription_type, 4, MSG_WAITALL) == 4) {
                        std::lock_guard<std::mutex> lock(clients_mutex_);
                        for (auto& client : clients_) {
                            if (client.fd == client_fd) {
                                client.subscriptions = subscription_type;
                                std::cerr << "[IPC] Client fd=" << client_fd 
                                          << " subscribed to types=" << subscription_type << std::endl;
                                break;
                            }
                        }
                    }
                }
            }
            // Skip other message types for now
            else if (header.payload_size > 0) {
                std::vector<uint8_t> payload(header.payload_size);
                recv(client_fd, payload.data(), header.payload_size, MSG_WAITALL);
            }
        }
        
        // Remove client
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
                                         [client_fd](const Client& c) { return c.fd == client_fd; }),
                          clients_.end());
            std::cerr << "[IPC] Client removed, fd=" << client_fd << std::endl;
        }
        close(client_fd);
    }

    void workerLoop() {
        while (running_) {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait_for(lock, std::chrono::milliseconds(100), 
                             [this] { return !message_queue_.empty() || !running_; });

            if (!running_) break;

            std::vector<QueueItem> items;
            while (!message_queue_.empty() && items.size() < 100) {
                items.push_back(std::move(message_queue_.front()));
                message_queue_.pop();
            }
            lock.unlock();

            for (const auto& item : items) {
                broadcastMessage(item.type, item.data);
            }
        }
    }

    void broadcastMessage(MessageType type, const std::vector<uint8_t>& data) {
        uint32_t subscription_mask = 0;
        switch (type) {
            case MessageType::EXTERNAL_MESSAGE:
                subscription_mask = static_cast<uint32_t>(SubscriptionType::EXTERNAL_MESSAGES);
                break;
            case MessageType::NEW_BLOCK:
                subscription_mask = static_cast<uint32_t>(SubscriptionType::NEW_BLOCKS);
                break;
            default:
                return;
        }

        MessageHeader header;
        header.type = type;
        header.payload_size = static_cast<uint32_t>(data.size());

        std::vector<uint8_t> packet;
        packet.reserve(sizeof(header) + data.size());
        packet.insert(packet.end(), (uint8_t*)&header, (uint8_t*)&header + sizeof(header));
        packet.insert(packet.end(), data.begin(), data.end());

        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = clients_.begin();
        while (it != clients_.end()) {
            if ((it->subscriptions & subscription_mask) == 0) {
                ++it;
                continue;
            }

            // Send with MSG_NOSIGNAL to prevent SIGPIPE
            ssize_t sent = send(it->fd, packet.data(), packet.size(), MSG_NOSIGNAL);
            if (sent < 0) {
                // Connection broken, remove client
                close(it->fd);
                it = clients_.erase(it);
            } else {
                it->last_activity = std::chrono::steady_clock::now();
                ++it;
            }
        }
    }

    void enqueueMessage(MessageType type, std::vector<uint8_t>&& data, Stats& stats) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (message_queue_.size() >= config_.max_queue_size) {
            stats.dropped_messages++;
            return;
        }
        message_queue_.push({type, std::move(data)});
        queue_cv_.notify_one();
    }

    std::vector<uint8_t> serializeExternalMessage(const ExternalMessageData& msg) {
        std::vector<uint8_t> data;
        // Simple serialization: [timestamp][source_len][source][dest_len][dest][data_len][data][hash_len][hash]
        
        data.resize(sizeof(uint64_t));
        memcpy(data.data(), &msg.timestamp_ms, sizeof(uint64_t));
        
        auto writeString = [&data](const std::string& s) {
            uint32_t len = static_cast<uint32_t>(s.size());
            size_t pos = data.size();
            data.resize(pos + sizeof(uint32_t) + len);
            memcpy(data.data() + pos, &len, sizeof(uint32_t));
            memcpy(data.data() + pos + sizeof(uint32_t), s.data(), len);
        };
        
        writeString(msg.source_addr);
        writeString(msg.dest_addr);
        
        uint32_t data_len = static_cast<uint32_t>(msg.data.size());
        size_t pos = data.size();
        data.resize(pos + sizeof(uint32_t) + data_len);
        memcpy(data.data() + pos, &data_len, sizeof(uint32_t));
        memcpy(data.data() + pos + sizeof(uint32_t), msg.data.data(), data_len);
        
        writeString(msg.hash);
        
        return data;
    }

    std::vector<uint8_t> serializeBlock(const BlockData& block) {
        std::vector<uint8_t> data;
        
        auto writeString = [&data](const std::string& s) {
            uint32_t len = static_cast<uint32_t>(s.size());
            size_t pos = data.size();
            data.resize(pos + sizeof(uint32_t) + len);
            memcpy(data.data() + pos, &len, sizeof(uint32_t));
            memcpy(data.data() + pos + sizeof(uint32_t), s.data(), len);
        };
        
        writeString(block.block_id);
        
        size_t pos = data.size();
        data.resize(pos + sizeof(uint32_t) * 4 + sizeof(uint8_t));
        memcpy(data.data() + pos, &block.gen_utime, sizeof(uint32_t));
        memcpy(data.data() + pos + sizeof(uint32_t), &block.delay_seconds, sizeof(uint32_t));
        memcpy(data.data() + pos + 2 * sizeof(uint32_t), &block.account_count, sizeof(uint32_t));
        memcpy(data.data() + pos + 3 * sizeof(uint32_t), &block.transaction_count, sizeof(uint32_t));
        uint8_t has_raw = block.has_raw_data ? 1 : 0;
        memcpy(data.data() + pos + 4 * sizeof(uint32_t), &has_raw, sizeof(uint8_t));
        
        // Transaction hashes
        uint32_t tx_count = static_cast<uint32_t>(block.transaction_hashes.size());
        pos = data.size();
        data.resize(pos + sizeof(uint32_t));
        memcpy(data.data() + pos, &tx_count, sizeof(uint32_t));
        
        for (const auto& hash : block.transaction_hashes) {
            writeString(hash);
        }
        
        // Raw block data if available
        if (block.has_raw_data) {
            uint32_t raw_size = static_cast<uint32_t>(block.raw_block_data.size());
            pos = data.size();
            data.resize(pos + sizeof(uint32_t) + raw_size);
            memcpy(data.data() + pos, &raw_size, sizeof(uint32_t));
            memcpy(data.data() + pos + sizeof(uint32_t), block.raw_block_data.data(), raw_size);
        }
        
        return data;
    }

private:
    Config config_;
    std::atomic<bool> running_{false};
    int server_fd_ = -1;
    
    std::vector<Client> clients_;
    std::mutex clients_mutex_;
    
    std::queue<QueueItem> message_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    std::thread acceptor_thread_;
    std::vector<std::thread> workers_;
    
    friend class IPCService;
};

// IPCService implementation
IPCService::IPCService(const Config& config) : impl_(std::make_unique<Impl>(config)) {}
IPCService::~IPCService() = default;

bool IPCService::start() { return impl_->start(); }
void IPCService::stop() { impl_->stop(); }

void IPCService::onExternalMessage(const ExternalMessageData& msg) {
    impl_->onExternalMessage(msg, stats_);
}

void IPCService::onNewBlock(const BlockData& block) {
    impl_->onNewBlock(block, stats_);
}

// Singleton instance
IPCService& getIPCService() {
    static IPCService instance;
    return instance;
}

} // namespace ton_ipc