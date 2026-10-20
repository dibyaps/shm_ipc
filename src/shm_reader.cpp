#include "../include/consumer.hpp"
#include <iostream>
#include <cstdint>

int main() {
    try {
        std::cout << "[Reader] Connecting to IPC Consumer...\n";
        ipc::Consumer consumer("/shm_ipc_test");

        std::cout << "[Reader] Connected to channel: " << consumer.channel_name() 
                  << ". Waiting for data...\n";

        const uint64_t messages_to_receive = 1000000;
        uint64_t received = 0;
        Slot current_slot;

        while (received < messages_to_receive) {
            if (consumer.receive(current_slot)) {
                // Verify sequence integrity
                if (current_slot.seq != received) {
                    std::cerr << "DATA CORRUPTION: Expected seq " << received
                              << " got " << current_slot.seq << "\n";
                    break;
                }
                received++;
            }
        }

        std::cout << "[Reader] Successfully read and verified " 
                  << received << " messages.\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[Reader Error] " << e.what() << "\n";
        std::cerr << "Did you run the writer first?\n";
        return 1;
    }
}
