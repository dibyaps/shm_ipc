#include <gtest/gtest.h>
#include "../include/producer.hpp"
#include "../include/consumer.hpp"
#include <thread>
#include <vector>
#include <cstring>

// =============================================================================
// SDK Integration Tests: Testing ONLY through ipc::Producer and ipc::Consumer
// =============================================================================

class SDKTest : public ::testing::Test {
protected:
    const std::string channel_name{"/sdk_test_channel"};

    void SetUp() override {
        // Clean up any stale SHM object if left behind by previous tests
        shm_unlink(channel_name.c_str());
    }

    void TearDown() override {
        // Always clean up after test finishes
        shm_unlink(channel_name.c_str());
    }
};

// 1. Consumer fails cleanly with an exception if Producer hasn't created channel
TEST_F(SDKTest, ConsumerFailsIfProducerDoesNotExist) {
    EXPECT_THROW({
        ipc::Consumer consumer(channel_name);
    }, std::system_error);
}

// 2. Standard SDK send() and receive() over real POSIX shared memory
TEST_F(SDKTest, StandardSendReceive) {
    ipc::Producer producer(channel_name);
    ipc::Consumer consumer(channel_name);

    EXPECT_EQ(producer.channel_name(), channel_name);
    EXPECT_EQ(consumer.channel_name(), channel_name);

    const char* payload = "SDK_STANDARD_PAYLOAD";
    uint32_t len = std::strlen(payload);

    producer.send(payload, len);

    Slot slot{};
    ASSERT_TRUE(consumer.receive(slot));
    EXPECT_EQ(slot.seq, 0);
    EXPECT_EQ(slot.len, len);
    EXPECT_EQ(std::memcmp(slot.payload, payload, len), 0);
}

// 3. True Zero-Copy via SDK: reserve() and commit() on Producer, peek() and commit() on Consumer
TEST_F(SDKTest, TrueZeroCopyReserveCommitViaSDK) {
    ipc::Producer producer(channel_name);
    ipc::Consumer consumer(channel_name);

    // Producer Phase 1: Reserve direct pointer in shared memory (NO local buffer!)
    Slot* slot = producer.reserve();
    ASSERT_NE(slot, nullptr);

    // Write directly into shared memory fields
    slot->len = 16;
    std::memcpy(slot->payload, "ZERO_COPY_DATA__", 16);

    // Producer Phase 2: Commit publication to consumer
    producer.commit();

    // Consumer Phase 1: Peek directly into shared memory (NO copy to local stack!)
    const Slot* peeked_slot = consumer.peek();
    ASSERT_NE(peeked_slot, nullptr);
    EXPECT_EQ(peeked_slot->seq, 0);
    EXPECT_EQ(peeked_slot->len, 16);
    EXPECT_EQ(std::memcmp(peeked_slot->payload, "ZERO_COPY_DATA__", 16), 0);

    // Consumer Phase 2: Commit read to advance index and free slot
    consumer.commit();

    // Queue is now empty
    EXPECT_EQ(consumer.peek(), nullptr);
}

// 4. Multi-message streaming using the SDK Zero-Copy path
TEST_F(SDKTest, ZeroCopyStreamMultipleMessages) {
    ipc::Producer producer(channel_name);
    ipc::Consumer consumer(channel_name);

    const size_t total_msgs = 100;

    for (size_t i = 0; i < total_msgs; ++i) {
        Slot* slot = producer.reserve();
        ASSERT_NE(slot, nullptr);

        slot->len = 4;
        slot->payload[0] = static_cast<uint8_t>(i);

        producer.commit();

        const Slot* read_slot = consumer.peek();
        ASSERT_NE(read_slot, nullptr);
        EXPECT_EQ(read_slot->seq, i);
        EXPECT_EQ(read_slot->payload[0], static_cast<uint8_t>(i));

        consumer.commit();
    }
}

#include <sys/wait.h>
#include <unistd.h>
#include <chrono>

// 5. Producer Move Semantics: Ownership transfer of SDK classes
TEST_F(SDKTest, ProducerMoveSemantics) {
    ipc::Producer p1(channel_name);

    // Move p1 into p2
    ipc::Producer p2 = std::move(p1);
    EXPECT_EQ(p2.channel_name(), channel_name);

    // p2 can successfully send
    const char* data = "MOVE_TEST";
    EXPECT_TRUE(p2.try_send(data, std::strlen(data)));

    // Consumer reads what p2 sent
    ipc::Consumer consumer(channel_name);
    Slot slot{};
    ASSERT_TRUE(consumer.receive(slot));
    EXPECT_EQ(std::memcmp(slot.payload, data, slot.len), 0);
}

// 6. Real Multi-Process Test (fork): Testing Producer and Consumer across separate Linux processes
TEST_F(SDKTest, MultiProcessCommunicationViaFork) {
    const uint64_t total_messages = 50000;

    // Create the Producer in parent process FIRST so channel exists
    ipc::Producer producer(channel_name);

    pid_t pid = fork();
    ASSERT_GE(pid, 0) << "fork() failed";

    if (pid == 0) {
        // -------------------------------------------------------------
        // CHILD PROCESS: Acts as Consumer in a completely separate address space
        // -------------------------------------------------------------
        try {
            ipc::Consumer consumer(channel_name);
            Slot slot{};
            for (uint64_t i = 0; i < total_messages; ++i) {
                if (!consumer.receive(slot)) {
                    _exit(1); // Error reading
                }
                if (slot.seq != i) {
                    _exit(2); // Sequence corruption
                }
            }
            _exit(0); // Success!
        } catch (...) {
            _exit(3); // Exception
        }
    } else {
        // -------------------------------------------------------------
        // PARENT PROCESS: Acts as Producer
        // -------------------------------------------------------------
        const char* msg = "CROSS_PROCESS_MSG";
        uint32_t len = std::strlen(msg);

        for (uint64_t i = 0; i < total_messages; ++i) {
            producer.send(msg, len);
        }

        // Wait for child process to finish and check its exit code
        int status = 0;
        waitpid(pid, &status, 0);

        ASSERT_TRUE(WIFEXITED(status)) << "Child process crashed or was killed";
        EXPECT_EQ(WEXITSTATUS(status), 0) << "Child process reported sequence corruption or error";
    }
}

// 7. Futex Sleep and Wakeup Verification: Consumer sleeps, Producer wakes it up after delay
TEST_F(SDKTest, FutexSleepAndWakeupAfterDelay) {
    ipc::Producer producer(channel_name);

    std::atomic<bool> consumer_started{false};
    std::atomic<bool> consumer_woken{false};

    // Consumer Thread: will wait on empty channel and go to sleep in futex
    std::thread consumer_thread([&]() {
        ipc::Consumer consumer(channel_name);
        consumer_started.store(true);

        Slot slot{};
        // This will spin 2,000 times, then SLEEP in the Linux kernel on FUTEX_WAIT
        bool ok = consumer.receive(slot);

        if (ok && slot.seq == 0) {
            consumer_woken.store(true);
        }
    });

    // Wait until consumer thread is running
    while (!consumer_started.load()) {
        std::this_thread::yield();
    }

    // Sleep in parent thread for 50 milliseconds to guarantee the consumer
    // exhausts its 2,000 spin cycles and is actively sleeping on FUTEX_WAIT in the kernel
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Consumer should STILL be sleeping waiting for data
    EXPECT_FALSE(consumer_woken.load());

    // Producer sends data now -> this MUST trigger futex_wake() and wake the consumer!
    const char* wake_msg = "WAKE_UP!";
    producer.send(wake_msg, std::strlen(wake_msg));

    // Join consumer thread: proves it woke up and finished
    consumer_thread.join();

    EXPECT_TRUE(consumer_woken.load()) << "Consumer was not woken up by producer!";
}
