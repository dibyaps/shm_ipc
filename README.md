# High-Performance Zero-Copy IPC Engine

[![CI Multi-Distro Build & Test](https://github.com/dibyaps/shm_ipc/actions/workflows/ci.yml/badge.svg)](https://github.com/dibyaps/shm_ipc/actions/workflows/ci.yml)
![Language](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)
![OS](https://img.shields.io/badge/OS-Rocky%20Linux%209%20%7C%20Ubuntu-orange.svg)
![License](https://img.shields.io/badge/License-MIT-green.svg)

A low-latency, lock-free **Single-Producer Single-Consumer (SPSC)** Inter-Process Communication (IPC) middleware engine implemented in modern **C++20** for Linux.

Designed for systems infrastructure, low-latency telemetry, and storage appliance architectures (targeting use cases at **Pure Storage, Nutanix, NetApp, and Akamai**).

---

## Performance Highlights

| Metric | Single-Copy (`memcpy`) | True Zero-Copy (`reserve`/`commit`) | Speedup |
| :--- | :--- | :--- | :--- |
| **Throughput** | 104.82 Million msg/sec | **270.04 Million msg/sec** | **~2.6x Faster** |
| **Round-Trip Latency** | 9.5 nanoseconds | **3.7 nanoseconds** | **~61% Lower Latency** |
| **Idle CPU Utilization** | 0.0% (Futex sleep) | 0.0% (Futex sleep) | Zero core starvation |
| **Data Corruption Rate** | 0% (500k concurrent stress) | 0% (500k concurrent stress) | Strict `acquire`/`release` |

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

### 1. Two-Phase In-Place Zero-Copy API
* Eliminates intermediate `memcpy` calls.
* **Producer:** `reserve()` returns a direct `Slot*` pointer into shared memory so data is constructed in-place; `commit()` publishes the write via `memory_order_release`.
* **Consumer:** `peek()` returns a direct `const Slot*` to inspect fields directly in shared memory; `commit()` frees the slot.

### 2. Cache-Line Alignment & Zero False Sharing
* Uses `alignas(64)` to place producer atomic indices (`write_index`), consumer atomic indices (`read_index`), and synchronization variables (`futex_word`) onto completely isolated CPU cache lines.
* Prevents cache-invalidation ping-pong across CPU L1/L2 caches under the MESI protocol.

### 3. C++20 Memory Model (Acquire-Release Semantics)
* Implements a lock-free ring buffer without `std::mutex` or heavy full-memory barriers (`mfence`).
* Uses `std::memory_order_release` when publishing slot updates and `std::memory_order_acquire` when consuming slot indices, ensuring memory visibility invariants at minimal CPU cycle cost.

### 4. Adaptive Hybrid-Futex Synchronization
* Avoids the standard trade-off between 100% CPU burn (pure spinning) and high context-switch latency (pure sleep).
* **Fast path:** Consumer spins for up to 2,000 iterations using the SSE2 `_mm_pause()` intrinsic to catch micro-bursts of traffic in user space.
* **Slow path:** If the spin budget is exhausted without new data, the consumer transitions into the Linux kernel via `syscall(SYS_futex, ..., FUTEX_WAIT)` to yield CPU back to the scheduler (0% CPU idle).
* **Selective Wake:** Producer checks the consumer's futex word and only issues `syscall(SYS_futex, ..., FUTEX_WAKE)` if the consumer is actually asleep, bypassing kernel transitions during active streams.

### 5. RAII Shared Memory Lifecycle (`ipc::ShmRegion`)
* Encapsulates POSIX shared memory (`shm_open`, `ftruncate`, `mmap`, `munmap`, `close`, `shm_unlink`) in an exception-safe RAII class.
* Strictly move-only (`= delete` on copy operations) to prevent double-free crashes.
* Automatically unlinks segments from `/dev/shm` on process exit.

---

## Directory Structure

```text
shm_ipc/
├── Makefile                    # Build configuration (-std=c++20 -O3 -Wall -Wextra -lrt)
├── README.md                   # Project documentation, architecture & benchmarks
├── plan.md                     # Roadmap and milestone tracker
├── .github/
│   └── workflows/ci.yml        # Multi-Distro CI (Rocky Linux 9 & Ubuntu 24.04)
├── include/
│   ├── ipc_layout.hpp          # Cache-aligned IPCHeader (192B) & Slot (64B)
│   ├── ring_buffer.hpp         # Lock-free SPSC queue with hybrid futex & zero-copy
│   ├── shm_region.hpp          # RAII POSIX shared memory lifecycle wrapper
│   ├── producer.hpp            # High-level Producer SDK
│   └── consumer.hpp            # High-level Consumer SDK
├── src/
│   ├── shm_writer.cpp          # Producer standalone application
│   └── shm_reader.cpp          # Consumer standalone application
├── tests/
│   ├── test_layout.cpp         # Memory alignment & struct size verifications
│   ├── test_shm_region.cpp     # RAII shared memory lifecycle & unlinking tests
│   ├── test_ring_buffer.cpp    # Queue invariants, wrap-around & 500k thread stress
│   └── test_sdk.cpp            # SDK integration, Zero-Copy, fork() multi-process & futex
└── benchmarks/
    └── bench_zerocopy.cpp      # 10M-iteration Single-Copy vs. Zero-Copy benchmark
```

---

## Quick Start & Usage

### Prerequisites
* Linux (Kernel 5.x+ / RHEL, Rocky Linux, Ubuntu, Debian)
* GCC 14+ or Clang 18+ (C++20 support)
* Google Test (`gtest-devel` on RHEL/Rocky, `libgtest-dev` on Ubuntu)

### Build All Targets
```bash
make clean && make all
```

### Running the Automated Google Test Suite (18 Tests)
```bash
make test
```
*Automatically generates JUnit XML and terminal log reports in `reports/test_report.xml` and `reports/test_run.log`.*

### Running the Performance Benchmark
```bash
make bench
```

### Running the Standalone Cross-Process Demo

Open two terminal windows:

#### Terminal 1: Launch the Producer
```bash
taskset -c 1 ./build/writer
```

#### Terminal 2: Launch the Consumer
```bash
taskset -c 2 ./build/reader
```
*Press Enter in Terminal 1 to stream 1,000,000 messages across processes.*

---

## Using the SDK in External Applications

### 1. True Zero-Copy (Fastest: ~270M msg/s)
```cpp
#include "include/producer.hpp"

ipc::Producer producer("/telemetry_bus");

// Phase 1: Reserve direct pointer in shared memory (NO copy!)
Slot* slot = producer.reserve();
if (slot) {
    // Write data directly into shared memory fields
    slot->len = 16;
    std::memcpy(slot->payload, "STORAGE_BLOCK_4K", 16);

    // Phase 2: Publish atomically to consumer
    producer.commit();
}
```

```cpp
#include "include/consumer.hpp"

ipc::Consumer consumer("/telemetry_bus");

// Phase 1: Inspect data directly in shared memory (NO copy to stack!)
const Slot* slot = consumer.peek();
if (slot) {
    process_data(slot->payload, slot->len);

    // Phase 2: Advance read index and release slot
    consumer.commit();
}
```

### 2. Standard Value-Copy API (Convenient)
```cpp
// Producer:
producer.send(my_data, sizeof(my_data));

// Consumer:
Slot slot;
if (consumer.receive(slot)) {
    // Process slot
}
```
