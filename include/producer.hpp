#ifndef IPC_PRODUCER_HPP
#define IPC_PRODUCER_HPP

#include "shm_region.hpp"
#include "ring_buffer.hpp"
#include <string>
#include <cstdint>

namespace ipc {

/**
 * @brief High-performance, lock-free IPC Producer.
 * 
 * Owns the underlying POSIX shared memory segment (as creator)
 * and publishes messages into the lock-free SPSC Ring Buffer.
 */
class Producer {
private:
    // NOTE: In C++, members are initialized in the exact order they are declared.
    // shm_ MUST be declared before ring_ so the memory is allocated before RingBuffer attaches!
    ShmRegion  shm_;
    RingBuffer ring_;
    uint64_t   seq_{0};

public:
    /**
     * @brief Construct a new Producer for a named channel.
     * @param channel_name POSIX shared memory name (e.g., "/my_channel")
     */
    explicit Producer(const std::string& channel_name)
        : shm_(ShmRegion::create(channel_name, SHM_SIZE)),
          ring_(shm_.get(), true) {} // true = we are the creator (initializes atomics)

    // =========================================================================
    // Ownership Semantics (Move-only, Non-copyable)
    // =========================================================================
    Producer(const Producer&) = delete;
    Producer& operator=(const Producer&) = delete;

    // Use default move operations: the compiler will automatically move shm_ and ring_!
    Producer(Producer&&) noexcept = default;
    Producer& operator=(Producer&&) noexcept = default;

    /**
     * @brief Phase 1 (Zero-Copy): Reserve a raw Slot in shared memory.
     * Allows the caller to populate the payload in-place without any memcpy!
     * @return Pointer to Slot in SHM, or nullptr if buffer is full.
     */
    Slot* reserve() {
        Slot* slot = ring_.reserve_write();
        if (slot) {
            slot->seq = seq_++; // Stamp sequence ID automatically
        }
        return slot;
    }

    /**
     * @brief Phase 2 (Zero-Copy): Commit the reserved slot and publish to consumer.
     */
    void commit() {
        ring_.commit_write();
    }

    /**
     * @brief Non-blocking write.
     * @param data Pointer to the payload bytes
     * @param len Length of the payload in bytes
     * @return true if published, false if the ring buffer is currently full.
     */
    bool try_send(const void* data, uint32_t len) {
        bool ok = ring_.try_write(seq_, reinterpret_cast<const uint8_t*>(data), len);
        if (ok) {
            ++seq_; // Only increment sequence number on successful publication
        }
        return ok;
    }

    /**
     * @brief Blocking write with CPU pause hint.
     * Spins until space becomes available in the ring buffer.
     */
    void send(const void* data, uint32_t len) {
        while (!try_send(data, len)) {
            _mm_pause(); // x86 pause instruction to reduce pipeline stalls & power
        }
    }

    /// Access the current sequence number
    uint64_t sequence() const noexcept { return seq_; }

    /// Access the underlying channel name
    const std::string& channel_name() const noexcept { return shm_.name(); }
};

} // namespace ipc

#endif // IPC_PRODUCER_HPP
