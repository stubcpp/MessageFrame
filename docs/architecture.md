# Architecture & Internals

This document provides a deep dive into the memory layouts, optimization decisions, and internal architectural mechanics of the `MessageFrame` library.

## 📐 Three-Part Message Layout

MessageFrame enforces a strict separation of concerns within a single serialized byte stream. This allows specialized network routers or intermediaries to inspect routing metadata without spending CPU cycles on parsing the actual payload data.
```text
+----------------------------------------------------------------------------------------------------+
|                                                                                                    |
|  HEADER (Fixed size: 36 bytes)                                                                     |
|  [Timestamp] [Message_Count] [Source ID] [Target ID] [Message_ID] [Message_Type] [Version] [Flags] |
+----------------------------------------------------------------------------------------------------+
|                                                                                                    |
|  STRUCTURED PARAMETERS (Variable size, MessagePack)                                                |
|  [Device 1] -> [Param A: Value] [Param B: Value] [Param C: Value]                                  |
|  [Device 2] -> [Param C: Value]                                                                    |
+----------------------------------------------------------------------------------------------------+
|                                                                                                    |
|  RAW BINARY ATTACHMENTS (Variable size, Append-only)                                               |
|  [Blob 1: Raw Bytes] [Blob 2: Raw Bytes] ...                                                       |
+----------------------------------------------------------------------------------------------------+
```

1. **Header (Fixed-Size, 36 bytes):** Contains predictable fields for addressing, sequencing, filtering, and payload length descriptors. It can be read atomically from a socket or DMA ring buffer.
2. **Parameters (Structured Metadata):** A flexible key-value ecosystem powered by compliant MessagePack encoding. Designed for small configuration states, status codes, and low-rate telemetry metrics.
3. **Attachments (Raw Binary Blobs):** Appended to the very end of the stream as completely raw byte sequences. Ideal for high-bandwidth raw arrays (e.g., SDR IQ-samples, spectrum captures, or image frames), **completely eliminating double-buffering or translation overhead**.

## 🧠 Cache-Friendly Parameter Storage (`HybridMessageMap`)

The inner storage of parameters relies on a custom, adaptive hybrid container designed to optimize memory layouts against CPU L1/L2 cache lines based on operational workloads.

[ Workload <= 128 elements ] -> Flat contiguous std::vector (CPU cache-local, O(N) search but O(1) on tiny sets)
[ Workload > 128 elements  ] -> Automatic transition to tsl::robin_map (Open-addressing hash map, O(1) lookups)

### 🏎️ The Vector Phase (Default `< SMALL_CAPACITY`)
Up to `SMALL_CAPACITY` parameters (hardcoded to **128 entries**), the internal storage uses a flat `std::vector<std::pair<ParameterKey, ParameterValue>>`.
* **Zero Fragmentation:** All elements sit contiguously in memory.
* **Hardware Prefetcher Friendly:** Modern CPUs fetch adjacent elements into cache lines automatically. For small workloads, a tight sequential loop doing linear scans (`O(N)`) outperforms the math overhead of calculating hashes (`O(1)`).

### ⚡ The Hash Map Phase (Beyond Threshold)
The moment the 129th parameter is injected, the engine dynamically triggers an internal layout migration:
1. A `tsl::robin_map` (Robin Hood hashing with open-addressing) is instantiated on the heap.
2. All existing 128 elements are transferred from the vector into the new map.
3. The internal state flag flips to `is_vector_mode = false`.

Because `tsl::robin_map` stores its buckets in a contiguous array rather than chained linked-lists (unlike standard `std::unordered_map`), it preserves maximum cache locality even at scale, ensuring point lookups (`find()`) complete in roughly **60 nanoseconds**.


## ⚙️ Allocation Tuning via `FrameConfig`

The `FrameConfig` object is an optimization override. It **does not alter the 128-element threshold ceiling**, but it gives the developer manual control over the initial state machinery to eliminate runtime spikes.

If you anticipate large-scale messages up front, you can instantiate the object with an explicit reservation hint:

```cpp
msgframe::FrameConfig config;
config.initial_reserve = 1024; // Express explicit parameter workload expectations

// Initialize the top-level frame
msgframe::MessageFrame msg(
    MyMsgId::TELEMETRY_PACKET,
    MyMsgType::CRITICAL,
    /*src_id=*/50,
    /*tgt_id=*/99,
    /*msg_cnt=*/1,
    /*proto_version=*/1,
    /*msg_flags=*/0,
    config);

// Bypasses the flat vector completely; instantiates tsl::robin_map with a 1024-slot reserve
for (int i = 0; i < 1024; ++i) {
    msg.add("bench", ("param_" + std::to_string(i)).c_str(), msgframe::VALUE(i));
}

### ♻️ Frame Recycling Loop Mechanics

When reusing a `MessageFrame` instance inside a critical processing loop via the `msg.clear()` method, the `initial_reserve` hint **is fully preserved**.

* **Without Hint:** `clear()` resets the container back to an empty `std::vector` (causing a repeated cycle of vector allocation ➔ fill ➔ map allocation ➔ data migration ➔ table rehashing on every single iteration).
* **With Hint:** `clear()` flushes the elements but **retains the fully allocated `tsl::robin_map` memory blocks**. The container immediately restarts in map mode, keeping subsequent insertions completely allocation-free and dropping insertion execution costs by **over 53%**.


## 🗂️ Project Workspace Layout

```
├── include/
│   └── messageframe/
│       ├── Header.hpp             # Fixed-size message header
│       ├── Value.hpp              # Tagged-union ParameterValue (int64/double/bool/string)
│       ├── HybridMessageMap.hpp   # Vector-to-hash-map container (pImpl facade)
│       ├── Structures.hpp         # Shared types (FlatKey, Attachment, FrameConfig)
│       └── MessageFrame.hpp       # Top-level message: header + parameters + attachments
├── src/
│   ├── Header.cpp                 #
│   ├── Value.cpp                  #
│   ├── HybridMessageMap.cpp       # Keeps <tsl/robin_map.h> as a private implementation detail
│   └── MessageFrame.cpp
├── third_party/                   # Vendored header-only dependencies
│   ├── robin_map/                 # tsl::robin_map
│   └── msgpack/                   # MessagePack serialization/deserialization
├── examples/
│   ├── basic_usage.cpp            # Minimal demonstration of the API
│   └── extended_usage.cpp         # Extended API: add/set/update, FlatKey, FrameConfig, error handling, edge cases
├── docs/
│   ├── api-guide.md               # API Guide (Full usage example)
│   ├── architecture.md            # Architecture & Internals
│   ├── installation.md            # Installation & Build Guide
│   ├── for-ai-assistants.md       # For AI Assistants & LLMs
│   └── performance.md             # Benchmarks
├── benchmarks/
│   └── benchmark.cpp              # Parameterized performance benchmark (--iterations, --params, --reserve N)
├── tests/
│   ├── test_framework.hpp         # Zero-dependency test harness
│   ├── test_hybrid_map.cpp        # HybridMessageMap correctness tests
│   └── test_messageframe_proxy.cpp # MessageFrame proxy-method tests
├── CMakeLists.txt
├── run_benchmark.sh
└── run_benchmark.bat
```

