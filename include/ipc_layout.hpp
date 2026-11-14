#ifndef IPC_LAYOUT_HPP
#define IPC_LAYOUT_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#if defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h> // For _mm_pause()
#endif

// Standard x86_64 and ARM CPU cache line size
constexpr size_t CACHE_LINE_SIZE = 64;

// Capacity must be a power of 2 for fast bitwise modulo arithmetic later
constexpr size_t RING_BUFFER_CAPACITY = 1024;

// The control header that sits at the very beginning of the shared memory
struct alignas(CACHE_LINE_SIZE) IPCHeader {

    // ---- PRODUCER CACHE LINE ----
    // Only the producer write to this atomic.
    // We align it to 64 bytes so it starts exactly on a fresh cache line.
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> write_index{0};

    // The producer keeps a local cache of the consumer's position
    // to avoid hitting the atomic read_index across the memory bus unnecessarily.
    uint64_t cached_read_index{0};

    // ---- CONSUMER CACHE LINE ----
    // Only the consumer writes to this atomic.
    // Pushed to the next 64-byte boundary.
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> read_index{0};
    uint64_t cached_write_index{0};

    // ----SYSNCHRONIZATION CACHE LINE ----
    alignas(CACHE_LINE_SIZE) std::atomic<uint32_t> futex_word{0};

};

// The actual data payload block.
// We intentionally size this to be exactly 64 bytes to fit perfectly in a cache line.
struct Slot {
    uint64_t seq;           // 8 bytes (Sequence number to verify ordering)
    uint32_t len;           // 4 bytes (Length of actal payload)
    uint8_t payload[52];    // 52 bytes (Data)
                            // Total: 8 + 4 + 52 = 64 bytes
};

// ======================================================
// Compile-Time Shared Memory Safety Guarantees
// ======================================================

// 1. Hardware lock-free atomic guarantees (prevents fallback to process-private mutex tables)
static_assert(std::atomic<uint64_t>::is_always_lock_free, "std::atomic<uint64_t> must be lock-free for cross-process shared memory!");
static_assert(std::atomic<uint32_t>::is_always_lock_free, "std::atomic<uint32_t> must be lock-free for cross-process shared memory!");

// 2. Memory layout and C++ object lifetime guarantees
static_assert(std::is_standard_layout_v<IPCHeader>, "IPCHeader must have standard layout");
static_assert(std::is_trivially_copyable_v<IPCHeader>, "IPCHeader must be trivially copyable");
static_assert(std::is_trivially_destructible_v<IPCHeader>, "IPCHeader must be trivially destructible");

static_assert(std::is_standard_layout_v<Slot>, "Slot must have standard layout");
static_assert(std::is_trivially_copyable_v<Slot>, "Slot must be trivially copyable");
static_assert(sizeof(Slot) == CACHE_LINE_SIZE, "Slot size must be exactly 64 bytes");

// Portable CPU pause hint for spin-wait loops (reduces pipeline stalls & power)
inline void cpu_pause() noexcept {
    #if defined(__x86_64__) || defined(_M_X64)
        _mm_pause();
    #elif defined(__aarch64__)
        asm volatile("yield" ::: "memory");
    #else
        std::atomic_signal_fence(std::memory_order_seq_cst);
    #endif
}

#endif