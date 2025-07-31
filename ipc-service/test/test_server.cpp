/*
    Test server for TON IPC Service
*/

#include "../ipc_service.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <signal.h>
#include <sstream>
#include <iomanip>

std::atomic<bool> running{true};

void signalHandler(int) {
    running = false;
}

std::string generateRandomHex(size_t length) {
    static const char hex_chars[] = "0123456789abcdef";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += hex_chars[dis(gen)];
    }
    return result;
}

std::string generateAddress() {
    std::stringstream ss;
    ss << "0:" << generateRandomHex(64);
    return ss.str();
}

void generateTestMessages() {
    auto& ipc = ton_ipc::getIPCService();
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> msg_interval(100, 2000); // 100ms to 2s
    std::uniform_int_distribution<> data_size(100, 1000);
    
    while (running) {
        // Generate external message
        ton_ipc::ExternalMessageData msg;
        msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        msg.source_addr = "ext";
        msg.dest_addr = generateAddress();
        msg.data.resize(data_size(gen));
        std::generate(msg.data.begin(), msg.data.end(), [&]() { return gen() & 0xFF; });
        msg.hash = generateRandomHex(64);
        
        ipc.onExternalMessage(msg);
        
        std::cout << "[TEST] Sent external message to " << msg.dest_addr 
                  << " hash=" << msg.hash << std::endl;
        
        std::this_thread::sleep_for(std::chrono::milliseconds(msg_interval(gen)));
    }
}

void generateTestBlocks() {
    auto& ipc = ton_ipc::getIPCService();
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> block_interval(5000, 10000); // 5-10s
    std::uniform_int_distribution<> account_count(50, 200);
    std::uniform_int_distribution<> tx_count(100, 500);
    
    uint32_t block_seqno = 1000000;
    
    while (running) {
        // Generate block
        ton_ipc::BlockData block;
        
        std::stringstream ss;
        ss << "(-1,8000000000000000," << block_seqno++ << ")";
        block.block_id = ss.str();
        
        block.gen_utime = std::time(nullptr) - 5; // 5 seconds ago
        block.delay_seconds = 5;
        block.account_count = account_count(gen);
        block.transaction_count = tx_count(gen);
        
        // Add some transaction hashes (first 5)
        int tx_to_show = std::min(5, (int)block.transaction_count);
        for (int i = 0; i < tx_to_show; ++i) {
            block.transaction_hashes.push_back(generateRandomHex(64));
        }
        
        ipc.onNewBlock(block);
        
        std::cout << "[TEST] Sent block " << block.block_id 
                  << " accounts=" << block.account_count
                  << " txs=" << block.transaction_count << std::endl;
        
        std::this_thread::sleep_for(std::chrono::milliseconds(block_interval(gen)));
    }
}

void printStats() {
    auto& ipc = ton_ipc::getIPCService();
    
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        auto& stats = ipc.getStats();
        std::cout << "\n[STATS] Messages sent: " << stats.messages_sent
                  << ", Blocks sent: " << stats.blocks_sent
                  << ", Active clients: " << stats.active_clients
                  << ", Dropped: " << stats.dropped_messages << "\n" << std::endl;
    }
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::cout << "Starting TON IPC test server..." << std::endl;
    
    // Configure and start IPC service
    ton_ipc::IPCService::Config config;
    config.socket_path = "/tmp/ton-ipc.sock";
    config.max_clients = 100;
    config.max_queue_size = 10000;
    
    auto& ipc = ton_ipc::getIPCService();
    if (!ipc.start()) {
        std::cerr << "Failed to start IPC service" << std::endl;
        return 1;
    }
    
    std::cout << "IPC service started on " << config.socket_path << std::endl;
    
    // Start test data generators
    std::thread msg_thread(generateTestMessages);
    std::thread block_thread(generateTestBlocks);
    std::thread stats_thread(printStats);
    
    std::cout << "Generating test data... Press Ctrl+C to stop" << std::endl;
    
    // Wait for threads
    msg_thread.join();
    block_thread.join();
    stats_thread.join();
    
    // Stop service
    ipc.stop();
    std::cout << "IPC service stopped" << std::endl;
    
    return 0;
}