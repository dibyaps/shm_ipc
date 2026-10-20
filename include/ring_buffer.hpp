#ifndef RING_BUFFER_HPP
#define RING_BUFFER_HPP

#include "ipc_layout.hpp"
#include <new>          // For placement new
#include <cstring>      // For memcpy
#include <algorithm>    // For std::min
#include <linux/futex.h> // For FUTEX_WAIT, FUTEX_WAKE
#include <sys/syscall.h> // For SYS_futex
#include <unistd.h>      // For syscall()
#include <emmintrin.h>   // For _mm_pause() (compiler intrinsic for CPU pause)

// Total size of our shared memory mapping: Header + Array of Slots

constexpr size_t SHM_SIZE = sizeof(IPCHeader) + (RING_BUFFER_CAPACITY * sizeof(Slot));

class RingBuffer {
    private:
        IPCHeader* header;
        Slot*      slots;

        //Helper to invoke Linux SYS_futex WAIT
        static int futex_wait(std::atomic<uint32_t>* addr, uint32_t expected_val) {
            // Note: We use FUTEX_WAIT (0), NOT FUTEX_WAIT_PRIVATE
            // becasue our process share memeory across different address spaces.
            return syscall(SYS_futex,
                            reinterpret_cast<uint32_t*>(addr),
                            FUTEX_WAIT,
                            expected_val,
                            nullptr, nullptr, 0);
        }

        // Helper to invoke Linux SYS_futex WAKE
        static int futex_wake(std::atomic<uint32_t>* addr, int count = 1) {
            return syscall(SYS_futex,
                            reinterpret_cast<uint32_t*>(addr),
                            FUTEX_WAKE,
                            count,
                            nullptr, nullptr, 0);
        }
    
    public:
        // Attach the lock-free queue to the raw shared memory pointer
        explicit RingBuffer(void* shm_base, bool initialize) {
            header = static_cast<IPCHeader*>(shm_base);
            // The slot array starts immediately after the header
            slots = reinterpret_cast<Slot*>(static_cast<uint8_t*>(shm_base) + sizeof(IPCHeader));

            // Only the creator (Writer) initializes the atomic variables
            if (initialize) {
                new (header) IPCHeader();
            }
        }

        // =====================================================================
        // Legacy Single-Copy API (Uses memcpy)
        // =====================================================================

        bool try_write(uint64_t seq, const uint8_t* payload, uint32_t len) {
            Slot* slot = reserve_write();
            if (!slot) {
                return false;
            }

            slot->seq = seq;
            slot->len = std::min(len, static_cast<uint32_t>(sizeof(slot->payload)));
            std::memcpy(slot->payload, payload, slot->len);

            commit_write();
            return true;
        }

        // =====================================================================
        // True Zero-Copy API (Two-Phase: reserve -> in-place fill -> commit)
        // =====================================================================

        /**
         * @brief Phase 1 (Producer): Reserve a slot directly in shared memory.
         * @return Pointer to raw Slot in SHM, or nullptr if the buffer is full.
         */
        Slot* reserve_write() {
            uint64_t current_write = header->write_index.load(std::memory_order_relaxed);

            // Check if buffer is full using local cached_read_index
            if (current_write - header->cached_read_index >= RING_BUFFER_CAPACITY) {
                header->cached_read_index = header->read_index.load(std::memory_order_acquire);
                if (current_write - header->cached_read_index >= RING_BUFFER_CAPACITY) {
                    return nullptr; // Buffer full
                }
            }

            // Return direct pointer to slot in SHM! Caller fills it in-place.
            uint64_t slot_idx = current_write & (RING_BUFFER_CAPACITY - 1);
            return &slots[slot_idx];
        }

        /**
         * @brief Phase 2 (Producer): Commit the reserved slot and publish to consumer.
         */
        void commit_write() {
            uint64_t current_write = header->write_index.load(std::memory_order_relaxed);

            // 1. Publish the write to the consumer
            // RELEASE semantics: ensures caller's in-place writes finish BEFORE write_index increments
            header->write_index.store(current_write + 1, std::memory_order_release);

            // 2. Wake the consumer ONLY if it is sleeping on the futex
            if (__builtin_expect(header->futex_word.load(std::memory_order_seq_cst) == 1, 0)) {
                header->futex_word.store(0, std::memory_order_relaxed);
                futex_wake(&header->futex_word, 1);
            }
        }

        /**
         * @brief Phase 1 (Consumer): Inspect the next available slot in-place.
         * @return Pointer to const Slot directly in SHM, or nullptr if empty.
         */
        const Slot* peek_read() {
            uint64_t current_read = header->read_index.load(std::memory_order_relaxed);

            // Check if buffer is empty
            if (header->cached_write_index - current_read == 0) {
                header->cached_write_index = header->write_index.load(std::memory_order_acquire);
                if (header->cached_write_index - current_read == 0) {
                    return nullptr; // Buffer empty
                }
            }

            // Return direct pointer to slot in SHM! Caller reads without copying.
            uint64_t slot_idx = current_read & (RING_BUFFER_CAPACITY - 1);
            return &slots[slot_idx];
        }

        /**
         * @brief Phase 2 (Consumer): Advance read_index after inspecting the slot.
         */
        void commit_read() {
            uint64_t current_read = header->read_index.load(std::memory_order_relaxed);
            // RELEASE semantics: producer can reclaim this slot
            header->read_index.store(current_read + 1, std::memory_order_release);
        }

        bool try_read(Slot& out_slot) {
            const Slot* slot = peek_read();
            if (!slot) {
                return false;
            }
            out_slot = *slot; // Copy
            commit_read();
            return true;
        }

        // Hybrid spin-then-sleep read
        bool read_blocking(Slot& out_slot, uint32_t max_spins = 2000) {

            while (true) {
                // Phase 1: Adaptive Spin-Wait (Sub-microsecond fast path)

                for (uint32_t i = 0; i < max_spins; ++i) {
                    if (try_read(out_slot)) {
                        return true;
                    }
                    _mm_pause(); // CPU pause hint to prevent pipeline stalls
                }

                // Phase 2: Spin budget exhausted. Prepare to sleep.
                // We signal: "I am going to sleep (value 1)"
                // We use memory_order_seq_cst to guarantee the producer sees this
                // store before we re-check the queue.
                header->futex_word.store(1, std::memory_order_seq_cst);

                // Phase 3: The Double-Check!
                // Did the producer push data right before we declared we are sleeping?
                if (try_read(out_slot)) {
                    // Data arrived! Cancle sleep and return immediately.
                    header->futex_word.store(0, std::memory_order_relaxed);
                    return true;
                }

                // Phase 4: Kernel SLeep (0% CPU idle)
                // If futex_word is stilll 1, kernel puts us to sleep
                // If producer updated futex_word in the meantime, wait returns immediately.
                futex_wait(&header->futex_word, 1);

                // Phase 5: Woken up! Reset state to awake (0) and retry reading.
                header->futex_word.store(0, std::memory_order_relaxed);

            }


        }

};

#endif