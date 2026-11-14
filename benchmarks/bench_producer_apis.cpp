#include "../include/producer.hpp"
#include "../include/consumer.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/utsname.h>
#include <unistd.h>

// Helper to inspect and print system hardware and OS environment
static void print_system_info() {
    utsname buf{};
    uname(&buf);

    std::string cpu_model = "Unknown CPU";
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (cpuinfo.is_open()) {
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.find("model name") != std::string::npos) {
                auto pos = line.find(':');
                if (pos != std::string::npos) {
                    cpu_model = line.substr(pos + 2);
                    break;
                }
            }
        }
    }

    std::string os_name = buf.sysname;
    std::ifstream os_rel("/etc/os-release");
    if (os_rel.is_open()) {
        std::string line;
        while (std::getline(os_rel, line)) {
            if (line.rfind("PRETTY_NAME=", 0) == 0) {
                os_name = line.substr(12);
                if (!os_name.empty() && os_name.front() == '"') os_name.erase(0, 1);
                if (!os_name.empty() && os_name.back() == '"') os_name.pop_back();
                break;
            }
        }
    }

    long ncores = sysconf(_SC_NPROCESSORS_ONLN);

    std::cout << "  [System Environment]\n";
    std::cout << "    CPU Model:    " << cpu_model << " (" << ncores << " logical cores)\n";
    std::cout << "    OS & Kernel:  " << os_name << " (" << buf.sysname << " " << buf.release << " " << buf.machine << ")\n";
    std::cout << "    Architecture: " << buf.machine << " | Cache Line: " << CACHE_LINE_SIZE << " bytes\n";
    std::cout << "    Compiler:     GCC " << __VERSION__ << "\n";
}

// 64-byte market order / telemetry record
struct alignas(8) MarketOrder {
    uint64_t timestamp_ns;
    uint64_t order_id;
    double   price;
    uint32_t qty;
    uint32_t flags;
    char     symbol[32]; // Total: 8 + 8 + 8 + 4 + 4 + 32 = 64 bytes
};

static_assert(sizeof(MarketOrder) == 64, "MarketOrder must be exactly 64 bytes");

int main() {
    const std::string channel_name = "/bench_producer_apis_channel";
    shm_unlink(channel_name.c_str());

    const uint64_t iterations = 5000000; // 5 Million iterations per benchmark

    std::cout << "========================================================================\n";
    std::cout << "     IPC SDK Producer APIs Head-to-Head Performance Benchmark           \n";
    std::cout << "     Message Size: 64 bytes | Iterations: " << iterations << " msgs per test     \n";
    std::cout << "------------------------------------------------------------------------\n";
    print_system_info();
    std::cout << "========================================================================\n\n";

    // =========================================================================
    // Benchmark 1: Zero-Copy (reserve() + commit() Producer, peek() + commit() Consumer)
    // =========================================================================
    {
        shm_unlink(channel_name.c_str());
        ipc::Producer producer(channel_name);
        ipc::Consumer consumer(channel_name);

        auto start = std::chrono::high_resolution_clock::now();

        for (uint64_t i = 0; i < iterations; ++i) {
            // Producer: Reserve slot directly in shared memory (Phase 1)
            Slot* slot = nullptr;
            while ((slot = producer.reserve()) == nullptr) {
                cpu_pause();
            }

            // Zero-Copy: Write directly into shared memory without local memcpy
            slot->len = sizeof(MarketOrder);
            auto* order = reinterpret_cast<MarketOrder*>(slot->payload);
            order->timestamp_ns = i;
            order->order_id = i;
            order->price = 142.50;
            order->qty = 100;
            order->flags = 1;

            // Producer: Commit publication (Phase 2)
            producer.commit();

            // Consumer: Inspect slot directly in shared memory (Zero-Copy peek)
            const Slot* read_slot = nullptr;
            while ((read_slot = consumer.peek()) == nullptr) {
                cpu_pause();
            }

            // In-place read from shared memory
            [[maybe_unused]] volatile uint64_t read_id = reinterpret_cast<const MarketOrder*>(read_slot->payload)->order_id;

            // Consumer: Commit read
            consumer.commit();
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;
        double throughput = (iterations / diff.count()) / 1e6;
        double ns_per_op = (diff.count() * 1e9) / iterations;

        std::cout << "[1] Zero-Copy API (reserve() + commit()):\n";
        std::cout << "    Copies:       0 (in-place in shared memory)\n";
        std::cout << "    Elapsed:      " << std::fixed << std::setprecision(4) << diff.count() << " seconds\n";
        std::cout << "    Throughput:   " << std::fixed << std::setprecision(2) << throughput << " Million msg/sec\n";
        std::cout << "    Round-Trip:   " << std::fixed << std::setprecision(1) << ns_per_op << " ns per message\n\n";
    }

    // =========================================================================
    // Benchmark 2: Non-Blocking Single-Copy (try_send() Producer, try_receive() Consumer)
    // =========================================================================
    {
        shm_unlink(channel_name.c_str());
        ipc::Producer producer(channel_name);
        ipc::Consumer consumer(channel_name);

        MarketOrder local_order{};
        local_order.price = 142.50;
        local_order.qty = 100;
        local_order.flags = 1;
        std::memcpy(local_order.symbol, "AAPL_US_EQUITY", 15);

        Slot out_slot{};

        auto start = std::chrono::high_resolution_clock::now();

        for (uint64_t i = 0; i < iterations; ++i) {
            local_order.timestamp_ns = i;
            local_order.order_id = i;

            // Producer: Non-blocking try_send (copies local_order -> shared memory slot)
            while (!producer.try_send(&local_order, sizeof(local_order))) {
                cpu_pause();
            }

            // Consumer: Non-blocking try_receive (copies shared memory slot -> out_slot)
            while (!consumer.try_receive(out_slot)) {
                cpu_pause();
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;
        double throughput = (iterations / diff.count()) / 1e6;
        double ns_per_op = (diff.count() * 1e9) / iterations;

        std::cout << "[2] Non-Blocking Single-Copy API (try_send()):\n";
        std::cout << "    Copies:       1 memcpy (in) + 1 memcpy (out)\n";
        std::cout << "    Elapsed:      " << std::fixed << std::setprecision(4) << diff.count() << " seconds\n";
        std::cout << "    Throughput:   " << std::fixed << std::setprecision(2) << throughput << " Million msg/sec\n";
        std::cout << "    Round-Trip:   " << std::fixed << std::setprecision(1) << ns_per_op << " ns per message\n\n";
    }

    // =========================================================================
    // Benchmark 3: Blocking Single-Copy (send() Producer, receive() Consumer)
    // =========================================================================
    {
        shm_unlink(channel_name.c_str());
        ipc::Producer producer(channel_name);
        ipc::Consumer consumer(channel_name);

        MarketOrder local_order{};
        local_order.price = 142.50;
        local_order.qty = 100;
        local_order.flags = 1;
        std::memcpy(local_order.symbol, "AAPL_US_EQUITY", 15);

        Slot out_slot{};

        auto start = std::chrono::high_resolution_clock::now();

        for (uint64_t i = 0; i < iterations; ++i) {
            local_order.timestamp_ns = i;
            local_order.order_id = i;

            // Producer: Blocking send() (spins with cpu_pause() until slot is free)
            producer.send(&local_order, sizeof(local_order));

            // Consumer: Blocking receive() (spins with cpu_pause(), then futex)
            consumer.receive(out_slot);
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;
        double throughput = (iterations / diff.count()) / 1e6;
        double ns_per_op = (diff.count() * 1e9) / iterations;

        std::cout << "[3] Blocking Single-Copy API (send()):\n";
        std::cout << "    Copies:       1 memcpy (in) + 1 memcpy (out)\n";
        std::cout << "    Elapsed:      " << std::fixed << std::setprecision(4) << diff.count() << " seconds\n";
        std::cout << "    Throughput:   " << std::fixed << std::setprecision(2) << throughput << " Million msg/sec\n";
        std::cout << "    Round-Trip:   " << std::fixed << std::setprecision(1) << ns_per_op << " ns per message\n\n";
    }

    std::cout << "========================================================================\n";
    shm_unlink(channel_name.c_str());
    return 0;
}
