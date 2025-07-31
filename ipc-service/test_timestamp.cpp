#include <iostream>
#include <chrono>

int main() {
    auto now = std::chrono::high_resolution_clock::now();
    uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    
    std::cout << "Nanoseconds since epoch: " << ns << std::endl;
    std::cout << "As seconds: " << ns / 1000000000.0 << std::endl;
    std::cout << "Current Unix time: " << time(nullptr) << std::endl;
    
    return 0;
}
