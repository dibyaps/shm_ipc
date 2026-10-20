#include "../include/ring_buffer.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <cstring>

// Simulating a realistic 52-byte storage I/O or market telemetry packet
struct TelemetryPacket {
    uint64_t timestamp_ns;
    uint32_t device_id;
    uint32_t error_code;
    char     status_msg[36]; // Total: 8 + 4 + 4 + 36 = 52 bytes
};

int main() {
    alignas(64) uint8_t memory[SHM_SIZE];
    RingBuffer queue(memory, true);

    const uint64_t iterations = 10000000; // 10 Million iterations
    std::cout << "=========================================================\n";
    std::cout << "  IPC Performance Benchmark: Single-Copy vs. Zero-Copy   \n";
    std::cout << "  Iterations: " << iterations << " messages (52 bytes each) \n";
    std::cout << "=========================================================\n\n";

    // -------------------------------------------------------------------------
    // Benchmark 1: The Single-Copy Approach (memcpy in + memcpy out)
    // -------------------------------------------------------------------------
    {
        TelemetryPacket local_packet;
        local_packet.device_id = 101;
        local_packet.error_code = 0;
        std::strcpy(local_packet.status_msg, "STATUS_OK_DEVICE_READY");

        Slot out_slot{};

        auto start = std::chrono::high_resolution_clock::now();

        for (uint64_t i = 0; i < iterations; ++i) {
            local_packet.timestamp_ns = i;

            // 1. Write: Copies local_packet into SHM slot via memcpy
            while (!queue.try_write(i, reinterpret_cast<const uint8_t*>(&local_packet), sizeof(local_packet))) {
                _mm_pause();
            }

            // 2. Read: Copies SHM slot into out_slot via memcpy
            while (!queue.try_read(out_slot)) {
                _mm_pause();
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;
        double throughput = (iterations / diff.count()) / 1e6;
        double ns_per_op = (diff.count() * 1e9) / iterations;

        std::cout << "[1] Single-Copy (memcpy):\n";
        std::cout << "    Time taken:   " << std::fixed << std::setprecision(4) << diff.count() << " seconds\n";
        std::cout << "    Throughput:   " << std::fixed << std::setprecision(2) << throughput << " Million msg/sec\n";
        std::cout << "    Latency:      " << std::fixed << std::setprecision(1) << ns_per_op << " ns per round-trip\n\n";
    }

    // -------------------------------------------------------------------------
    // Benchmark 2: The Zero-Copy Approach (In-Place reserve/commit + peek/commit)
    // -------------------------------------------------------------------------
    {
        auto start = std::chrono::high_resolution_clock::now();

        for (uint64_t i = 0; i < iterations; ++i) {
            // 1. Reserve: Get direct pointer into shared memory (Phase 1)
            Slot* slot = nullptr;
            while ((slot = queue.reserve_write()) == nullptr) {
                _mm_pause();
            }

            // 2. Zero-Copy: Write directly into shared memory fields (NO memcpy!)
            slot->seq = i;
            slot->len = sizeof(TelemetryPacket);
            auto* packet = reinterpret_cast<TelemetryPacket*>(slot->payload);
            packet->timestamp_ns = i;
            packet->device_id = 101;
            packet->error_code = 0;

            // 3. Commit: Publish to consumer (Phase 2)
            queue.commit_write();

            // 4. Zero-Copy Read: Inspect slot directly in SHM (NO copy to stack!)
            const Slot* read_slot = nullptr;
            while ((read_slot = queue.peek_read()) == nullptr) {
                _mm_pause();
            }

            // Read field directly from shared memory!
            [[maybe_unused]] volatile uint64_t seq = read_slot->seq;

            // 5. Commit Read: Advance read index
            queue.commit_read();
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;
        double throughput = (iterations / diff.count()) / 1e6;
        double ns_per_op = (diff.count() * 1e9) / iterations;

        std::cout << "[2] Zero-Copy (In-Place reserve/commit):\n";
        std::cout << "    Time taken:   " << std::fixed << std::setprecision(4) << diff.count() << " seconds\n";
        std::cout << "    Throughput:   " << std::fixed << std::setprecision(2) << throughput << " Million msg/sec\n";
        std::cout << "    Latency:      " << std::fixed << std::setprecision(1) << ns_per_op << " ns per round-trip\n\n";
    }

    std::cout << "=========================================================\n";
    return 0;
}
