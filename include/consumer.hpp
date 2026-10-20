#ifndef IPC_CONSUMER_HPP
#define IPC_CONSUMER_HPP

#include "shm_region.hpp"
#include "ring_buffer.hpp"
#include <string>
#include <cstdint>

namespace ipc {

/**
 * @brief High-performance, lock-free IPC Consumer.
 * 
 * Attaches to an existing POSIX shared memory segment (as consumer)
 * and reads messages from the lock-free SPSC Ring Buffer using
 * hybrid spin-then-futex waiting.
 */
class Consumer {
private:
    // NOTE: shm_ MUST be declared before ring_ so the memory is attached first!
    ShmRegion  shm_;
    RingBuffer ring_;

public:
    /**
     * @brief Construct a new Consumer for an existing named channel.
     * @param channel_name POSIX shared memory name (e.g., "/my_channel")
     */
    explicit Consumer(const std::string& channel_name)
        : shm_(ShmRegion::open(channel_name, SHM_SIZE)),
          ring_(shm_.get(), false) {} // false = do not re-initialize atomics

    // =========================================================================
    // Ownership Semantics (Move-only, Non-copyable)
    // =========================================================================
    Consumer(const Consumer&) = delete;
    Consumer& operator=(const Consumer&) = delete;

    Consumer(Consumer&&) noexcept = default;
    Consumer& operator=(Consumer&&) noexcept = default;

    /**
     * @brief Non-blocking read.
     * @param out_slot Reference to Slot structure to receive the data
     * @return true if a message was read, false if the queue is empty.
     */
    bool try_receive(Slot& out_slot) {
        return ring_.try_read(out_slot);
    }

    /**
     * @brief Hybrid spin-then-sleep blocking read.
     * 
     * Spins for max_spins iterations with _mm_pause() to catch micro-bursts,
     * then transitions to Linux SYS_futex (FUTEX_WAIT) to sleep at 0% CPU.
     * 
     * @param out_slot Reference to Slot structure to receive the data
     * @param max_spins Number of spin iterations before sleeping (default: 2000)
     * @return true when a message is successfully received.
     */
    bool receive(Slot& out_slot, uint32_t max_spins = 2000) {
        return ring_.read_blocking(out_slot, max_spins);
    }

    /// Access the underlying channel name
    const std::string& channel_name() const noexcept { return shm_.name(); }
};

} // namespace ipc

#endif // IPC_CONSUMER_HPP
