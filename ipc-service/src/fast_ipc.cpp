/*
    Fast IPC Service Implementation
*/

#include "fast_ipc.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <errno.h>

namespace ton_ipc {

// Get high-resolution timestamp in nanoseconds
static uint64_t getNanoTimestamp() {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
}

// IPCChannel implementation
IPCChannel::IPCChannel(const std::string& socket_path, ChannelType type) 
    : socket_path_(socket_path), channel_type_(type) {
}

IPCChannel::~IPCChannel() {
    stop();
}

bool IPCChannel::start() {
    if (running_.exchange(true)) {
        return false; // Already running
    }
    
    // Create socket (use SOCK_STREAM on macOS as SOCK_SEQPACKET is not well supported)
#ifdef __APPLE__
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
#else
    server_fd_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
#endif
    if (server_fd_ < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        running_ = false;
        return false;
    }
    
    // Set non-blocking
    int flags = fcntl(server_fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::cerr << "Failed to set non-blocking: " << strerror(errno) << std::endl;
        close(server_fd_);
        running_ = false;
        return false;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Set large buffers (32MB)
    int bufsize = 32 * 1024 * 1024;
    setsockopt(server_fd_, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
    setsockopt(server_fd_, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
    
    // Remove existing socket file
    unlink(socket_path_.c_str());
    
    // Bind
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
    
    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind to " << socket_path_ << ": " << strerror(errno) << std::endl;
        close(server_fd_);
        running_ = false;
        return false;
    }
    
    // Listen
    if (listen(server_fd_, 128) < 0) {
        std::cerr << "Failed to listen: " << strerror(errno) << std::endl;
        close(server_fd_);
        unlink(socket_path_.c_str());
        running_ = false;
        return false;
    }
    
    // Start accept thread
    std::thread(&IPCChannel::acceptLoop, this).detach();
    
    std::cout << "IPC channel started on " << socket_path_ << std::endl;
    return true;
}

void IPCChannel::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    // Close server socket
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
    
    // Close all clients
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& client : clients_) {
            close(client.fd);
        }
        clients_.clear();
    }
    
    // Remove socket file
    unlink(socket_path_.c_str());
}

void IPCChannel::acceptLoop() {
    while (running_) {
        // Accept new connections
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (running_) {
                std::cerr << "Accept error: " << strerror(errno) << std::endl;
            }
            continue;
        }
        
        // Set non-blocking
        int flags = fcntl(client_fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
        }
        
        // Set large send buffer
        int bufsize = 32 * 1024 * 1024;
        setsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
        
        // Add client
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_.push_back({client_fd, getNanoTimestamp()});
            clients_connected_ = clients_.size();
        }
        
        std::cout << "Client connected to " << socket_path_ << ", total: " << clients_connected_.load() << std::endl;
    }
}

void IPCChannel::broadcast(const uint8_t* data, size_t length) {
    if (!running_ || length == 0) {
        return;
    }
    
    // Prepare message header
    FastMessage header;
    memset(&header, 0, sizeof(header));
    header.timestamp_ns = getNanoTimestamp();
    header.data_length = static_cast<uint32_t>(length);
    
    // Use scatter-gather I/O for single syscall
    struct iovec iov[2];
    iov[0].iov_base = &header;
    iov[0].iov_len = sizeof(header);
    iov[1].iov_base = const_cast<uint8_t*>(data);
    iov[1].iov_len = length;
    
    // Send to all clients
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto it = clients_.begin();
    while (it != clients_.end()) {
        ssize_t sent = writev(it->fd, iov, 2);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer full, skip this client
                ++it;
                continue;
            }
            // Connection broken
            close(it->fd);
            it = clients_.erase(it);
            clients_connected_ = clients_.size();
        } else {
            ++it;
            messages_sent_++;
            bytes_sent_ += sent;
        }
    }
}

void IPCChannel::removeClient(int fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.erase(
        std::remove_if(clients_.begin(), clients_.end(),
                      [fd](const Client& c) { return c.fd == fd; }),
        clients_.end()
    );
    clients_connected_ = clients_.size();
}

uint64_t IPCChannel::getNanoTimestamp() const {
    return ::ton_ipc::getNanoTimestamp();
}

// FastIPCService implementation
bool FastIPCService::start() {
    // Create channels
    extmsg_channel_ = std::make_unique<IPCChannel>("/tmp/ton-extmsg.ipc", ChannelType::EXTERNAL_MESSAGE);
    blocks_channel_ = std::make_unique<IPCChannel>("/tmp/ton-blocks.ipc", ChannelType::NEW_BLOCK);
    
    // Start both channels
    if (!extmsg_channel_->start()) {
        std::cerr << "Failed to start external message channel" << std::endl;
        return false;
    }
    
    if (!blocks_channel_->start()) {
        std::cerr << "Failed to start blocks channel" << std::endl;
        extmsg_channel_->stop();
        return false;
    }
    
    std::cout << "Fast IPC service started successfully" << std::endl;
    return true;
}

void FastIPCService::stop() {
    if (extmsg_channel_) {
        extmsg_channel_->stop();
    }
    if (blocks_channel_) {
        blocks_channel_->stop();
    }
}

void FastIPCService::sendExternalMessage(const uint8_t* data, size_t length) {
    if (extmsg_channel_) {
        extmsg_channel_->broadcast(data, length);
    }
}

void FastIPCService::sendNewBlock(const uint8_t* data, size_t length) {
    if (blocks_channel_) {
        blocks_channel_->broadcast(data, length);
    }
}

} // namespace ton_ipc