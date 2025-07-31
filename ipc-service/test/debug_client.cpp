#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>

struct FastMessage {
    uint64_t timestamp_ns;
    uint32_t data_length;
    uint8_t  reserved[4];
} __attribute__((packed));

void debugChannel(const std::string& name, const std::string& socket_path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << name << ": Failed to create socket" << std::endl;
        return;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << name << ": Failed to connect" << std::endl;
        close(fd);
        return;
    }
    
    std::cout << name << ": Connected" << std::endl;
    
    uint8_t buffer[65536];
    size_t pending = 0;
    int msg_count = 0;
    
    // Set non-blocking
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    while (msg_count < 20) { // Read first 20 messages
        // Try to read more data
        ssize_t n = recv(fd, buffer + pending, sizeof(buffer) - pending, 0);
        if (n > 0) {
            pending += n;
            std::cout << name << ": Read " << n << " bytes, pending=" << pending << std::endl;
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            std::cerr << name << ": Read error: " << strerror(errno) << std::endl;
            break;
        }
        
        // Process complete messages
        while (pending >= sizeof(FastMessage)) {
            FastMessage* hdr = (FastMessage*)buffer;
            size_t msg_size = sizeof(FastMessage) + hdr->data_length;
            
            if (pending < msg_size) {
                std::cout << name << ": Incomplete message, need " << msg_size << " have " << pending << std::endl;
                break;
            }
            
            auto recv_time = std::chrono::high_resolution_clock::now();
            auto recv_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                recv_time.time_since_epoch()).count();
            
            msg_count++;
            
            std::cout << name << " [" << msg_count << "]:" << std::endl;
            std::cout << "  Timestamp: " << hdr->timestamp_ns << " ns" << std::endl;
            std::cout << "  Recv time: " << recv_ns << " ns" << std::endl;
            std::cout << "  Latency: " << (recv_ns - hdr->timestamp_ns) / 1000.0 << " µs" << std::endl;
            std::cout << "  Data size: " << hdr->data_length << " bytes" << std::endl;
            std::cout << "  Total msg size: " << msg_size << " bytes" << std::endl;
            
            // Move remaining data
            pending -= msg_size;
            if (pending > 0) {
                memmove(buffer, buffer + msg_size, pending);
            }
        }
        
        // Small delay
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Timeout check
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::high_resolution_clock::now() - start_time).count();
        if (elapsed > 30) {
            std::cout << name << ": Timeout" << std::endl;
            break;
        }
    }
    
    close(fd);
    std::cout << name << ": Disconnected after " << msg_count << " messages" << std::endl;
}

int main() {
    std::cout << "Debug IPC Client" << std::endl;
    std::cout << "================" << std::endl;
    
    std::thread t1(debugChannel, "ExtMsg", "/tmp/ton-extmsg.ipc");
    std::thread t2(debugChannel, "Blocks", "/tmp/ton-blocks.ipc");
    
    t1.join();
    t2.join();
    
    return 0;
}