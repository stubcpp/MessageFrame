# Performance Benchmarks

*Tested on: Intel Core 7 240H, Ubuntu 22.04 (x64 Release, GCC).*
*Test Framework: Evaluated via `benchmarks/benchmark.cpp --iterations 200000 --params N`. Figures below reflect typical real-world performance results, not single best-case outliers. Run-to-run variance on this hardware environment is roughly ±10%.*

---

## 📈 Executive Summary

`MessageFrame` achieves massive throughput lines by adapting its underlying storage topography to the data size. For small messages (up to 128 elements), it leverages a contiguous, allocation-free `std::vector`. For larger messages, it transitions to a fast, open-addressing `tsl::robin_map`, which can be further optimized using an initialization sizing hint.

---

## 🏎️ Scenario A: Small Frame (4 parameters)
*Topology: Fixed Header + 4 scalar telemetry metrics, zero attachments.*
*Primary Mode: Flat, cache-local sequential array.*

| Metric | Measured Value |
| :--- | :--- |
| **Avg Time per Message (Full Cycle)** | **0.678 μs** |
| **Network Throughput** | ~1,473,936 messages/sec (**119.30 MB/sec**) |
| **Avg Packed Frame Size** | 84 bytes |
| **Microsecond Call Split** (`add` / `serialize` / `deserialize`) | 0.10 μs / 0.16 μs / 0.32 μs |

---

## 🛹 Scenario B: Peak Vector Streaming (127 parameters)
*Topology: Fixed Header + 127 metrics, zero attachments.*
*Primary Mode: Operating at the absolute ceiling threshold of the cache-friendly flat array, just before triggering hashing routines.*

| Metric | Measured Value |
| :--- | :--- |
| **Avg Time per Message (Full Cycle)** | **10.410 μs** |
| **Network Throughput** | ~96,009 messages/sec (**190.07 MB/sec**) |
| **Avg Packed Frame Size** | 2,075 bytes |
| **Microsecond Call Split** (`add` / `serialize` / `deserialize`) | 2.81 μs / 2.66 μs / 4.54 μs |

---

## ⚡ Scenario C: Large Frame (150 parameters)
*Topology: Fixed Header + 150 parameters.*
*Primary Mode: Automated runtime container migration to `tsl::robin_map` (open-addressing hash table) triggered at the 129th parameter.*

| Metric | Measured Value |
| :--- | :--- |
| **Avg Time per Message (Full Cycle)** | **22.030 μs** |
| **Network Throughput** | ~45,402 messages/sec (**107.80 MB/sec**) |
| **Avg Packed Frame Size** | 2,488 bytes |
| **Microsecond Call Split** (`add` / `serialize` / `deserialize`) | 8.47 μs / 3.45 μs / 8.98 μs |

---

## 🚀 Scenario D: Massive Frame Optimization (1024 parameters)
*Topology: Fixed Header + 1024 parameters. This scenario demonstrates the explicit cost of dynamic on-the-fly table reallocation versus an optimized pre-allocated sizing hint.*

When your application handles wide messages containing hundreds or thousands of keys, allowing the container to start in vector mode and dynamically scale up causes noticeable heap thrashing and bucket rehashing. By passing a `FrameConfig::initial_reserve = 1024` hint, the framework instantly provisions the hash table, keeping execution paths optimized and allocation-free.

| Performance Metric | Default Behavior (`--reserve 0`) | Sized Hint Applied (`--reserve 1024`) | Performance Delta |
| :--- | :---: | :---: | :---: |
| **Avg Time per Message** | 219.429 μs | **173.725 μs** | 📈 **20.83% Faster** |
| **Message Processing Rate** | 4,557 msgs/sec | **5,756 msgs/sec** | ⚡ **+1,199 msgs/sec** |
| **Effective Throughput** | 82.63 MB/sec | **104.37 MB/sec** | 🚀 **+21.74 MB/sec** |
| **Avg Packed Frame Size** | 19,012 bytes | **19,012 bytes** | Unchanged |
| **Parameter Insertion (`sum_add`)** | 88.14 μs | **41.09 μs** | 🔥 **53.38% Faster** |
| **Point Lookup (`sum_find` worst-case)**| 0.06 μs | **0.06 μs** | Stable $O(1)$ efficiency |
| **Encoding Cost (`sum_serialize`)** | 28.10 μs | **27.37 μs** | Identical code paths |
| **Decoding Cost (`sum_deserialize`)** | 85.03 μs | **85.10 μs** | Identical code paths |

### 🔍 Architectural Analysis of Scenario D:
* **The `sum_add` Breakthrough:** Pre-allocating slots for `tsl::robin_map` shrinks the execution costs of element insertion from **88.14 μs down to 41.09 μs** — a **53.38% gain** achieved solely by bypassing the vector-fill stage and preventing sequential memory re-allocations on the heap.
* **Point Lookup Resiliency:** Point lookups (`find()`) remain highly optimal at exactly **60 nanoseconds (`0.06 μs`)** even for the very last inserted element in a table of 1024 keys. This proves that Robin Hood hashing and contiguous internal bucket arrays maintain exceptional L1/L2 cache line hits.
