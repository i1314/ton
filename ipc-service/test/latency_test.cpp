/*
    IPC Latency Test - Measures actual round-trip latency
*/

#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <algorithm>
#include <numeric>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <cstring>
#include <netinet/tcp.h>

struct FastMessage {
    uint64_t timestamp_ns;
    uint32_t data_length;
    uint8_t  reserved[4];
} __attribute__((packed));

class LatencyTester {
private:
    int fd_;
    std::vector<double> latencies_;
    
public:
    bool connect(const std::string& socket_path) {
        fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd_ < 0) {
            return false;
        }
        
        // Set non-blocking
        int flags = fcntl(fd_, F_GETFL, 0);
        fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
        
        // Disable Nagle
        int one = 1;
        setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
        
        if (::connect(fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(fd_);
            return false;
        }
        
        return true;
    }
    
    void measureLatency(int num_messages) {
        uint8_t buffer[65536];
        latencies_.clear();
        latencies_.reserve(num_messages);
        
        // Wait for messages and measure latency
        int received = 0;
        auto start_time = std::chrono::high_resolution_clock::now();
        
        while (received < num_messages) {
            ssize_t n = recv(fd_, buffer, sizeof(buffer), 0);
            if (n < sizeof(FastMessage)) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                    continue;
                }
                break;
            }
            
            auto recv_time = std::chrono::high_resolution_clock::now();
            
            // Process messages in buffer
            size_t offset = 0;
            while (offset + sizeof(FastMessage) <= n) {
                FastMessage* msg = (FastMessage*)(buffer + offset);
                
                if (offset + sizeof(FastMessage) + msg->data_length > n) {
                    break; // Incomplete message
                }
                
                // Calculate latency in microseconds
                auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    recv_time - start_time).count();
                double latency_us = latency_ns / 1000.0;
                
                // Store only after warmup (skip first 10)
                if (received >= 10) {
                    latencies_.push_back(latency_us);
                }
                
                offset += sizeof(FastMessage) + msg->data_length;
                received++;
                
                if (received >= num_messages) break;
            }
            
            // Check timeout
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::high_resolution_clock::now() - start_time).count();
            if (elapsed > 10) {
                std::cerr << "Timeout waiting for messages" << std::endl;
                break;
            }
        }
    }
    
    void printStats() {
        if (latencies_.empty()) {
            std::cout << "No latency measurements collected" << std::endl;
            return;
        }
        
        std::sort(latencies_.begin(), latencies_.end());
        
        double avg = std::accumulate(latencies_.begin(), latencies_.end(), 0.0) / latencies_.size();
        double min = latencies_.front();
        double max = latencies_.back();
        double p50 = latencies_[latencies_.size() / 2];
        double p99 = latencies_[latencies_.size() * 99 / 100];
        
        std::cout << "\nLatency Statistics (microseconds):" << std::endl;
        std::cout << "  Min: " << min << " µs" << std::endl;
        std::cout << "  Avg: " << avg << " µs" << std::endl;
        std::cout << "  P50: " << p50 << " µs" << std::endl;
        std::cout << "  P99: " << p99 << " µs" << std::endl;
        std::cout << "  Max: " << max << " µs" << std::endl;
        std::cout << "  Samples: " << latencies_.size() << std::endl;
    }
    
    ~LatencyTester() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }
};

int main() {
    std::cout << "IPC Latency Test" << std::endl;
    std::cout << "================" << std::endl;
    
    // Test external message channel
    {
        std::cout << "\nTesting External Message Channel:" << std::endl;
        LatencyTester tester;
        if (!tester.connect("/tmp/ton-extmsg.ipc")) {
            std::cerr << "Failed to connect to external message channel" << std::endl;
            return 1;
        }
        
        tester.measureLatency(1000);
        tester.printStats();
    }
    
    // Test block channel
    {
        std::cout << "\nTesting Block Channel:" << std::endl;
        LatencyTester tester;
        if (!tester.connect("/tmp/ton-blocks.ipc")) {
            std::cerr << "Failed to connect to block channel" << std::endl;
            return 1;
        }
        
        tester.measureLatency(20); // Fewer blocks
        tester.printStats();
    }
    
    return 0;
}