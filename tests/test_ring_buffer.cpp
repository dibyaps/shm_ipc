#include <gtest/gtest.h>
#include "../include/ring_buffer.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <random>
#include <cstring>

// =============================================================================
// Low-Level RingBuffer Queue Invariants (Direct Memory Tests)
// =============================================================================

class RingBufferTest : public ::testing::Test {
protected:
    alignas(64) uint8_t memory[SHM_SIZE];
    RingBuffer* queue{nullptr};

    void SetUp() override {
        std::memset(memory, 0, sizeof(memory));
        queue = new RingBuffer(memory, true);
    }

    void TearDown() override {
        delete queue;
        queue = nullptr;
    }
};

// 1. Reading from an empty queue cleanly returns false
TEST_F(RingBufferTest, EmptyQueueReturnsFalse) {
    Slot slot{};
    EXPECT_FALSE(queue->try_read(slot));
}

// 2. Single message round-trip preserves seq, length, and payload
TEST_F(RingBufferTest, WriteAndReadSingleMessage) {
    const char* message = "HELLO_WORLD";
    uint32_t len = std::strlen(message);

    EXPECT_TRUE(queue->try_write(100, reinterpret_cast<const uint8_t*>(message), len));

    Slot out_slot{};
    ASSERT_TRUE(queue->try_read(out_slot));
    EXPECT_EQ(out_slot.seq, 100);
    EXPECT_EQ(out_slot.len, len);
    EXPECT_EQ(std::memcmp(out_slot.payload, message, len), 0);

    // Queue should now be empty again
    EXPECT_FALSE(queue->try_read(out_slot));
}

// 3. Buffer full condition: cannot exceed RING_BUFFER_CAPACITY (1024)
TEST_F(RingBufferTest, BufferFullPreventsOverrun) {
    const char* dummy = "DATA";

    for (size_t i = 0; i < RING_BUFFER_CAPACITY; ++i) {
        EXPECT_TRUE(queue->try_write(i, reinterpret_cast<const uint8_t*>(dummy), 4));
    }

    // 1025th message MUST fail
    EXPECT_FALSE(queue->try_write(9999, reinterpret_cast<const uint8_t*>(dummy), 4));

    // Drain 1 slot
    Slot out_slot{};
    ASSERT_TRUE(queue->try_read(out_slot));
    EXPECT_EQ(out_slot.seq, 0);

    // Now writing 1 slot must succeed again
    EXPECT_TRUE(queue->try_write(1024, reinterpret_cast<const uint8_t*>(dummy), 4));
}

// 4. Wrap-around modulo arithmetic: 5,000 messages on a 1,024-slot buffer
TEST_F(RingBufferTest, WrapAroundIntegrity) {
    const size_t total_messages = 5000;
    const char* text = "WRAP_TEST";

    for (size_t i = 0; i < total_messages; ++i) {
        ASSERT_TRUE(queue->try_write(i, reinterpret_cast<const uint8_t*>(text), 9));

        Slot slot{};
        ASSERT_TRUE(queue->try_read(slot));
        EXPECT_EQ(slot.seq, i);
    }
}

// 5. Binary payload fidelity: Random bytes & embedded nulls (\0)
TEST_F(RingBufferTest, RandomBinaryDataWithNullBytes) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> byte_dist(0, 255);
    std::uniform_int_distribution<uint32_t> len_dist(1, 52);

    for (size_t iter = 0; iter < 1000; ++iter) {
        uint32_t len = len_dist(rng);
        std::vector<uint8_t> payload(len);
        for (size_t j = 0; j < len; ++j) {
            payload[j] = static_cast<uint8_t>(byte_dist(rng));
        }

        ASSERT_TRUE(queue->try_write(iter, payload.data(), len));

        Slot slot{};
        ASSERT_TRUE(queue->try_read(slot));
        EXPECT_EQ(slot.seq, iter);
        EXPECT_EQ(slot.len, len);
        EXPECT_EQ(std::memcmp(slot.payload, payload.data(), len), 0);
    }
}

// 6. Oversized payload safety: Clamps to 52-byte maximum capacity
TEST_F(RingBufferTest, OversizedPayloadClampedGracefully) {
    uint8_t large_data[128];
    std::memset(large_data, 0xAB, sizeof(large_data));

    EXPECT_TRUE(queue->try_write(1, large_data, sizeof(large_data)));

    Slot slot{};
    ASSERT_TRUE(queue->try_read(slot));
    EXPECT_EQ(slot.len, 52);
    EXPECT_EQ(slot.payload[0], 0xAB);
    EXPECT_EQ(slot.payload[51], 0xAB);
}

// 7. Concurrent Stress Test: SPSC lock-free concurrency under real thread contention
TEST_F(RingBufferTest, ConcurrentSPSCStress500K) {
    const uint64_t total_messages = 500000;
    std::atomic<bool> producer_done{false};

    // Producer Thread
    std::thread producer_thread([&]() {
        const char* msg = "STRESS_TICK";
        uint32_t len = std::strlen(msg);
        for (uint64_t i = 0; i < total_messages; ++i) {
            while (!queue->try_write(i, reinterpret_cast<const uint8_t*>(msg), len)) {
                _mm_pause();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    // Consumer Thread
    uint64_t received = 0;
    Slot slot{};
    while (received < total_messages) {
        if (queue->try_read(slot)) {
            ASSERT_EQ(slot.seq, received) << "Sequence corruption detected!";
            received++;
        } else {
            _mm_pause();
        }
    }

    producer_thread.join();
    EXPECT_EQ(received, total_messages);
}
