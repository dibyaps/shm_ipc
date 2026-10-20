# High-Performance Zero-Copy IPC Engine

A low-latency, lock-free **Single-Producer Single-Consumer (SPSC)** Inter-Process Communication (IPC) transport engine implemented in modern **C++20** for Linux. 

---

## Performance Highlights

* **Throughput:** **>48 Million messages/second** (up to **~120M msg/s** under hot cache bursts) on commodity x86_64 Linux hardware.
* **Latency:** Single-digit nanoseconds per message (~8.2ns to 20ns).
* **CPU Utilization:** **0% CPU consumption** during idle periods via an adaptive Linux **hybrid-futex** control plane (no thread spinning starvation).
* **Integrity:** Zero message loss and zero data corruption verified across 1,000,000 serialized messages under concurrent stress.

---

## Architectural Features

```
+-----------------------------------------------------------------------------------+
|                        POSIX Shared Memory (/dev/shm)                             |
|                                                                                   |
|  +------------------------ IPCHeader (192 Bytes) ------------------------------+  |
|  |  Cache Line 0 (64B)  |  std::atomic<uint64_t> write_index, cached_read     |  |
|  |  Cache Line 1 (64B)  |  std::atomic<uint64_t> read_index, cached_write      |  |
|  |  Cache Line 2 (64B)  |  std::atomic<uint32_t> futex_word (sync state)       |  |
|  +-----------------------------------------------------------------------------+  |
|                                                                                   |
|  +------------------------ Slot Ring (1024 x 64B Slots) -----------------------+  |
|  | Slot 0 (64B): [seq (8B) | len (4B) | payload (52B)]                         |  |
|  | Slot 1 (64B): [seq (8B) | len (4B) | payload (52B)]                         |  |
|  | ...                                                                         |  |
|  | Slot 1023 (64B): [seq (8B) | len (4B) | payload (52B)]                      |  |
|  +-----------------------------------------------------------------------------+  |
+-----------------------------------------------------------------------------------+
```

### 1. Cache-Line Alignment & Zero False Sharing
* Uses `alignas(64)` to place producer atomic indices (`write_index`), consumer atomic indices (`read_index`), and synchronization variables (`futex_word`) onto completely isolated CPU cache lines.
* Prevents cache-invalidation ping-pong across CPU L1/L2 caches under the MESI protocol.

### 2. C++20 Memory Model (Acquire-Release Semantics)
* Implements a lock-free ring buffer without `std::mutex` or heavy full-memory barriers (`mfence`).
* Uses `std::memory_order_release` when publishing slot updates and `std::memory_order_acquire` when consuming slot indices, ensuring memory visibility invariants at minimal CPU cycle cost.

### 3. Adaptive Hybrid-Futex Synchronization
* Avoids the standard trade-off between 100% CPU burn (pure spinning) and high context-switch latency (pure sleep).
* **Fast path:** Consumer spins for up to 2,000 iterations using the SSE2 `_mm_pause()` intrinsic to catch micro-bursts of traffic in user space.
* **Slow path:** If the spin budget is exhausted without new data, the consumer transitions into the Linux kernel via `syscall(SYS_futex, ..., FUTEX_WAIT)` to yield CPU back to the scheduler (0% CPU idle).
* **Selective Wake:** Producer checks the consumer's futex word and only issues `syscall(SYS_futex, ..., FUTEX_WAKE)` if the consumer is actually asleep, bypassing kernel transitions during active streams.

### 4. RAII Shared Memory Lifecycle (`ipc::ShmRegion`)
* Encapsulates POSIX shared memory (`shm_open`, `ftruncate`, `mmap`, `munmap`, `close`, `shm_unlink`) in an exception-safe RAII class.
* Strictly move-only (`= delete` on copy operations) to prevent double-free crashes.
* Automatically unlinks segments from `/dev/shm` on process exit.

### 5. Pluggable Client SDK (`ipc::Producer` & `ipc::Consumer`)
* Encapsulates low-level memory mappings and atomics into a high-level, 2-line middleware API for external services.

---

## Directory Structure

```text
shm_ipc/
├── Makefile                # Build configuration (-std=c++20 -O3 -Wall -Wextra -lrt)
├── README.md               # Architecture documentation and benchmarks
├── plan.md                 # Complete development roadmap & milestone tracker
├── include/
│   ├── ipc_layout.hpp      # Cache-aligned IPCHeader (192B) & Slot (64B)
│   ├── ring_buffer.hpp     # Lock-free SPSC queue with hybrid futex
│   ├── shm_region.hpp      # RAII POSIX shared memory lifecycle wrapper
│   ├── producer.hpp        # High-level Producer SDK
│   └── consumer.hpp        # High-level Consumer SDK
└── src/
    ├── layout_test.cpp     # Cache-line alignment & offset verification
    ├── shm_writer.cpp      # Producer test application
    └── shm_reader.cpp      # Consumer test application
```

---

## Quick Start & Usage

### Prerequisites
* Linux (Kernel 5.x+ / RHEL, Rocky Linux, Ubuntu, Debian)
* GCC 14+ or Clang 18+ (C++20 support)
* POSIX Realtime Extensions library (`librt`)

### Build
```bash
make clean && make
```

### Running the End-to-End Test

Open two terminal windows:

#### Terminal 1: Launch the Writer (Producer)
```bash
taskset -c 1 ./build/writer
```
*(The writer initializes `/dev/shm/shm_ipc_test` and waits for user confirmation).*

#### Terminal 2: Launch the Reader (Consumer)
```bash
taskset -c 2 ./build/reader
```
*(The reader attaches to the channel and waits for data at 0.0% CPU).*

#### Stream Data
Press **Enter** in Terminal 1. The writer streams 1,000,000 messages and wakes the reader:
```text
[Writer] Sent 1000000 messages in 0.020803 seconds.
[Reader] Successfully read and verified 1000000 messages.
```

---

## Using the SDK in Your Own Applications

### Producer
```cpp
#include "include/producer.hpp"

int main() {
    ipc::Producer producer("/my_telemetry_bus");

    const char* payload = "DISK_IO_CMD_001";
    producer.send(payload, strlen(payload)); // Blocking or try_send() non-blocking
    return 0;
}
```

### Consumer
```cpp
#include "include/consumer.hpp"

int main() {
    ipc::Consumer consumer("/my_telemetry_bus");

    Slot slot;
    if (consumer.receive(slot)) { // Adaptive spin-then-futex
        // Process slot.seq, slot.len, slot.payload
    }
    return 0;
}
```

