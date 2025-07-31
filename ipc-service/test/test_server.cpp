/*
    Test server for Fast IPC
    Simulates TON node sending messages
*/

#include "../include/fast_ipc.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <signal.h>
#include <cstring>
#include <vector>

std::atomic<bool> running{true};

void signalHandler(int sig) {
    std::cout << "\nShutting down..." << std::endl;
    running = false;
}

// Generate test external message
std::vector<uint8_t> generateExternalMessage(size_t size) {
    std::vector<uint8_t> data(size);
    
    // Simulate TON external message structure
    // First 4 bytes: magic
    data[0] = 0x10; // ext_in_msg_info
    data[1] = 0x00;
    data[2] = 0x00;
    data[3] = 0x00;
    
    // Fill rest with random data
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    for (size_t i = 4; i < size; ++i) {
        data[i] = dis(gen);
    }
    
    return data;
}

// Generate test block
std::vector<uint8_t> generateBlock(size_t size) {
    std::vector<uint8_t> data(size);
    
    // Simulate TON block structure
    // First 4 bytes: block magic
    data[0] = 0xB5; // block magic
    data[1] = 0xEE;
    data[2] = 0x6C;
    data[3] = 0x35;
    
    // Add some structure
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    for (size_t i = 4; i < size; ++i) {
        data[i] = dis(gen);
    }
    
    return data;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::cout << "Fast IPC Test Server" << std::endl;
    std::cout << "====================" << std::endl;
    
    // Parse arguments
    int messages_per_second = 1000;
    int message_size = 1024;
    
    if (argc > 1) {
        messages_per_second = std::atoi(argv[1]);
    }
    if (argc > 2) {
        message_size = std::atoi(argv[2]);
    }
    
    std::cout << "Messages per second: " << messages_per_second << std::endl;
    std::cout << "Message size: " << message_size << " bytes" << std::endl;
    
    // Start IPC service
    auto& ipc = ton_ipc::FastIPCService::getInstance();
    if (!ipc.start()) {
        std::cerr << "Failed to start IPC service" << std::endl;
        return 1;
    }
    
    // Calculate sleep time
    auto sleep_us = 1000000 / messages_per_second;
    
    // Statistics
    uint64_t total_ext_messages = 0;
    uint64_t total_blocks = 0;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Wait a bit for clients to connect
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Main loop
    while (running) {
        // Send external message
        auto ext_msg = generateExternalMessage(message_size);
        ipc.sendExternalMessage(ext_msg.data(), ext_msg.size());
        total_ext_messages++;
        
        // Every 10 messages, send a block (changed from 100 for easier testing)
        if (total_ext_messages % 10 == 0) {
            auto block = generateBlock(message_size * 10); // Blocks are larger
            ipc.sendNewBlock(block.data(), block.size());
            total_blocks++;
        }
        
        // Sleep to control rate
        std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
        
        // Print stats every second
        if (total_ext_messages % messages_per_second == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            
            if (elapsed > 0) {
                std::cout << "Stats after " << elapsed << "s:" << std::endl;
                std::cout << "  External messages: " << total_ext_messages 
                          << " (" << total_ext_messages / elapsed << "/s)" << std::endl;
                std::cout << "  Blocks: " << total_blocks 
                          << " (" << total_blocks / elapsed << "/s)" << std::endl;
                
                auto ext_channel = ipc.getExternalMessageChannel();
                auto block_channel = ipc.getNewBlockChannel();
                
                if (ext_channel) {
                    std::cout << "  ExtMsg channel - clients: " << ext_channel->getClientsConnected()
                              << ", sent: " << ext_channel->getMessagesSent()
                              << ", bytes: " << ext_channel->getBytessSent() << std::endl;
                }
                
                if (block_channel) {
                    std::cout << "  Block channel - clients: " << block_channel->getClientsConnected()
                              << ", sent: " << block_channel->getMessagesSent()
                              << ", bytes: " << block_channel->getBytessSent() << std::endl;
                }
                
                std::cout << std::endl;
            }
        }
    }
    
    // Final stats
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_elapsed = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();
    
    std::cout << "\nFinal statistics:" << std::endl;
    std::cout << "Total runtime: " << total_elapsed << " seconds" << std::endl;
    std::cout << "Total external messages: " << total_ext_messages << std::endl;
    std::cout << "Total blocks: " << total_blocks << std::endl;
    std::cout << "Average rate: " << (total_elapsed > 0 ? total_ext_messages / total_elapsed : 0) << " msg/s" << std::endl;
    
    // Stop service
    ipc.stop();
    
    return 0;
}