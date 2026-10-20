#include "../include/producer.hpp"
#include <iostream>
#include <chrono>
#include <cstring>

int main() {
    try {
        std::cout << "[Writer] Initializing IPC Producer...\n";
        ipc::Producer producer("/shm_ipc_test");

        std::cout << "[Writer] Engine active on channel: " << producer.channel_name() << "\n";
        std::cout << "[Writer] Press Enter to start pumping data...\n";
        std::cin.get();

        const uint64_t messages_to_send = 1000000;
        const char* msg = "MARKET_TICK_AAPL";
        uint32_t len = std::strlen(msg);

        auto start_time = std::chrono::high_resolution_clock::now();

        for (uint64_t i = 0; i < messages_to_send; ++i) {
            producer.send(msg, len);
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end_time - start_time;

        std::cout << "[Writer] Sent " << messages_to_send 
                  << " messages in " << diff.count() << " seconds.\n";
        std::cout << "[Writer] Press Enter to cleanup and exit...\n";
        std::cin.get();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[Writer Error] " << e.what() << "\n";
        return 1;
    }
}
