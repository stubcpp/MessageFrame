# MessageFrame

A lightweight, header-only C++17 library for structured network messaging:
typed key-value parameters, MessagePack serialization, and raw binary attachments.
No schema files, no code generation. Add parameters and serialize in just two lines.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)

## What is this library for

Many telemetry and control systems rely on schema-based messaging frameworks like Google Protocol Buffers (Protobuf) or FlatBuffers. They are powerful, but they require predefined `.proto`/`.fbs` files and an ahead-of-time code generation step. This becomes a major bottleneck when the **message structure is determined at runtime** rather than fixed at compile time.

**MessageFrame** takes a different approach:
* **No schema files, no code generation:** Messages are built dynamically from typed key-value parameters.
* **All-in-one packet:** A single network frame carries small telemetry metrics alongside heavy, raw binary payloads (like IQ samples, spectra, or raw arrays).

### The Trade-off: Runtime Flexibility vs. Zero-Copy
MessageFrame deliberately trades zero-copy access for runtime flexibility. Unlike FlatBuffers, where data is read directly from the wire buffer, MessageFrame performs an explicit `deserialize()` step to rebuild its parameter map. This is the calculated price of eliminating `.proto` compilers from your build pipeline — a deliberate architectural trade-off, not an oversight.

## Core Concept: Two-Part Keys

Instead of designing a custom C-struct for every message variation or forcing devices into rigid object trees, MessageFrame addresses every parameter using a composite approach: a **device identifier** and a **parameter name**.

```cpp
// Address parameters flatly without nesting structures — types are preserved dynamically
msg.add("sdr_1",     "tx_gain",     msgframe::VALUE(10.0));
msg.add("sdr_1",     "sample_rate", msgframe::VALUE(2'000'000.0));
msg.add("sdr_2",     "rx_gain",     msgframe::VALUE(25.0));
msg.add("sdr_2",     "center_freq", msgframe::VALUE(433'000'000.0));
msg.add("core",      "firmware",    msgframe::VALUE("v1.3.5"));
msg.add("channel_1", "status_ok",   msgframe::VALUE(true));
```

This design naturally builds a logical `device ➔ parameter ➔ value` hierarchy inside a single network packet.

### Why this matters for system architecture:
* **Decoupled subsystems:** Isolated software modules or hardware drivers can safely dump their local telemetry into the same message frame without any prior knowledge of each other.
* **Zero structural maintenance:** There are no monolithic data structures or per-device serialization schemas to maintain, update, and distribute across nodes.
* **Plug-and-play scaling:** Adding a new device or metric to the stream is as trivial as invoking another `.add()` call at runtime.

## Key Features

* **Schema-less, yet strictly typed:** No `.proto`/`.fbs` files, no external compilers, and no code generation. Parameters preserve their underlying types (`int64_t`, `double`, `bool`, `std::string`) via `msgframe::VALUE`, accessed through a clean `msg.add(...)` / `msg.find(...)` API.
* **Three-part frame layout:** Every network frame physically separates a fixed-size **header** (allowing low-overhead routing without parsing the payload), compact key-value **parameters**, and uncompressed, heavy **binary attachments** transmitted as-is.
* **Cache-friendly hybrid storage:** Under the hood, parameters live in a flat, cache-local `std::vector` up to `SMALL_CAPACITY` (128 by default) for fast, allocation-free sequential access. It transparently switches to a high-performance hash map (`tsl::robin_map`) only when the parameter count exceeds the threshold.
* **Explicit memory tuning (`FrameConfig`):** If you anticipate a massive message workload, you can pass a sizing hint to bypass the vector stage entirely. The container will initialize directly in map mode with a pre-allocated capacity, eliminating migration and rehashing overhead.
* **Standard MessagePack wire format:** The parameter block serializes into compliant MessagePack payload data. Messages can be ingested and parsed by any standard MessagePack implementation across different language ecosystems.

> 📄 *Detailed deep-dives into the `HybridMessageMap` internals, the three-part frame layout, and `FrameConfig` benchmarks can be found in [docs/architecture.md](docs/architecture.md).*

## Typical Use Cases

* **Controlling multiple SDR nodes simultaneously:** A single multi-channel TX/RX SDR platform can expose dozens of configuration parameters (gain, sample rate, center frequency, bandwidth, filter modes). Each sub-module dumps data into the same message frame under its own device key, collapsing complex configurations into **a single atomic network payload**.
* **Aggregating fleet telemetry:** Perfect for gathering volatile metrics (temperature, supply voltages, RSSI, firmware versions, runtime error logs) from a distributed system without maintaining strict API schemas or breaking backward compatibility when a new metric is introduced.
* **Unified command and control (C2):** The flexible `device -> parameter` schema natively fits asymmetrical communication patterns. The same architecture handles configuration commands (e.g., set frequency, enable channel) and periodic status reports alike.
* **Streaming raw data with inline metadata:** The zero-overhead `attachments` pipeline allows you to bind raw binary blobs—such as high-rate IQ data chunks or spectrum snapshots—directly onto the structured metadata packet, **eliminating double-buffering or multi-socket alignment problems**.

## Quick Start

```cpp
#include <messageframe/MessageFrame.hpp>
#include <iostream>
#include <vector>

// Define your own strictly-typed application domains
enum class MyMsgId : int32_t {
    TELEMETRY_PACKET = 1001,
    COMMAND_PACKET   = 1002
};

enum class MyMsgType : int32_t {
    PERIODIC = 1,
    CRITICAL = 2
};

// 1. Initialize a message frame with explicit metadata (enums cast internally)
msgframe::MessageFrame msg(
    MyMsgId::TELEMETRY_PACKET,
    MyMsgType::CRITICAL,
    50,          // source_id
    99,          // target_id
    1,           // message_counter  [optional]
    1,           // protocol_version [optional]
    0x0001       // message_flags    [optional]
);

// 2. Dynamically add typed key-value parameters
msg.add("sensor_alpha", "voltage",    msgframe::VALUE(12.6));
msg.add("device_core",  "fw_version", msgframe::VALUE("v3.2.1"));

// 3. Serialize into a standard byte buffer
std::vector<uint8_t> buffer;
msg.serialize(buffer);

// 4. Deserialize and safely query data on the receiving end
msgframe::MessageFrame received;
if (received.deserialize(buffer.data(), buffer.size())) {
    if (const auto* val = received.find("device_core", "fw_version")) {
        // Type-safe retrieval using std::optional-like interfaces
        if (auto as_string = val->tryGetString()) {
            std::cout << "Firmware version: " << *as_string << "\n";
        }

        // Type mismatches are handled gracefully without runtime exceptions
        auto as_int = val->tryGetInt();
        std::cout << "tryGetInt() on a string value has_value() = "
                  << std::boolalpha << as_int.has_value() << "\n"; // Outputs: false
    }
}
```

> 📖 *For a full walkthrough covering **header layouts, raw binary attachments, parameter iteration, `add()` vs `set()` vs `update()` semantics, performance-critical `FlatKey` structures for hot loops, and frame recycling (`clear()`)**, check out the comprehensive [API Guide](docs/api-guide.md).*

## Installation & Build

No system-wide package managers are required. All dependencies (`msgpack-c`, `tsl::robin_map`) are vendored internally as Git submodules.

### 1. Clone the repository recursively
```bash
git clone --recursive https://github.com/stubcpp/MessageFrame.git
cd MessageFrame
```
*If you cloned without `--recursive`, run `git submodule update --init --recursive` before building.*

### 2. Build via CMake

#### 🐧 Linux / macOS (GCC / Clang)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

#### 🪟 Windows (Visual Studio / MSVC)
```bash
cmake -B build
cmake --build build --config Release
```

> ⚙️ *For advanced integration methods—such as using automated **turnkey helper scripts**, integrating directly via **CMake `FetchContent`**, or performing **manual source-only (copy-paste) embedding**—please refer to the full [Installation Guide](docs/installation.md).*

## Performance & Benchmarks

MessageFrame is engineered for zero-overhead execution on critical data paths. Below are typical real-world benchmarks measured on an **Intel Core 7 240H** (Ubuntu 22.04, GCC, Release build).

⚠️ **Crucial Note:** Unless specified otherwise, full cycle times reflect the **complete end-to-end pipeline** per message: dynamic parameter insertion (`add`) ➔ encoding (`serialize`) ➔ wire decoding (`deserialize`).

### Full End-to-End Pipeline Performance Snapshot

| Payload Scenario | Full Cycle Time (Add+Serialize+Deserialize) | Throughput | Packed Size | Primary Storage Mode |
| :--- | :--- | :--- | :--- | :--- |
| **Small Frame** (4 parameters) | **0.68 μs** | ~1.47M msgs/sec | 84 bytes | Flat `std::vector` (Cache-local) |
| **Peak Vector** (127 parameters) | **10.41 μs** | ~96K msgs/sec | 2,075 bytes | Flat `std::vector` (Threshold ceiling) |
| **Large Frame** (150 parameters) | **22.03 μs** | ~45K msgs/sec | 2,488 bytes | Transparent `tsl::robin_map` switch |
| **Massive Frame** (1024 parameters, with hint) | **173.72 μs** | ~5.7K msgs/sec | 19,012 bytes | Pre-allocated `tsl::robin_map` (Bypassed vector) |

---

### 🎯 The Power of Allocation Tuning (`FrameConfig`)

When dealing with massive payloads (e.g., **1024 parameters** per message), migrating from a vector to a hash map on the fly causes a visible performance hit due to heap reallocations and table rehashing.

By passing a `FrameConfig::initial_reserve` hint, you instruct the internal engine to skip the vector phase completely and instantiate a pre-sized `tsl::robin_map` up front.

| Benchmark Metric (1024 params / msg) | Default Behavior (Lazy Sizing) | Optimized Behavior (With 1024 Hint) | Performance Delta |
| :--- | :---: | :---: | :---: |
| **Parameter Insertion (`sum_add`)** | 88.14 μs | **41.09 μs** | ⚡ **53.4% Faster** |
| **Total Time per Message** | 219.43 μs | **173.72 μs** | 📈 **20.8% Faster** |
| **Network Throughput** | 82.63 MB/sec | **104.37 MB/sec** | 🚀 **+21.74 MB/sec** |
| **Point Lookup (`sum_find` worst-case)** | 0.06 μs | 0.06 μs | Identical $O(1)$ efficiency |

### Key Takeaways:
* **Sub-Microsecond Lookups:** Thanks to open-addressing in `tsl::robin_map`, fetching the very last inserted key (`find()`) out of 1024 elements takes a mere **60 nanoseconds** (`0.06 μs`).
* **Serialization Efficiency:** MessagePack effortlessly packs a massive 19 KB key-value payload in **~27 μs**, making it a perfect fit for multi-device high-rate telemetry lines.

> 📊 *For full micro-benchmarks breaking down internal layout topologies and execution costs across different hardware targets, see the [Performance Benchmarks Guide](docs/performance.md).*

## Documentation & Deep-Dives

MessageFrame is fully documented across dedicated sub-guides. Pick the topic that matches your immediate integration task:

* 📐 **[Architecture & Internals](docs/architecture.md)** — Deep-dive into the `HybridMessageMap` layout mechanics, memory switching thresholds, and binary attachment boundaries.
* 💻 **[API & Usage Guide](docs/api-guide.md)** — A complete, actionable reference covering `add()` vs `set()` vs `update()` semantic differences, `clear()` loop recycling, and hot-path lookups.
* 📦 **[Installation & Integration](docs/installation.md)** — Step-by-step setup walkthroughs for native CMake configuration, Git submodules, `FetchContent` streaming, or raw source embedding.
* 📈 **[Performance Benchmarks](docs/performance.md)** — Comprehensive runtime execution matrixes, profiling specs, and hardware environment parameters.
* 🤖 **[Guidance for AI Assistants](docs/for-ai-assistants.md)** — **Crucial for LLM users!** Strict prompt instructions, anti-hallucination rules, and strict code-gen guardrails optimized for **Cursor, GitHub Copilot, Claude, and ChatGPT** integrations.

## Contributing

Contributions are highly appreciated! Whether you are fixing a bug, optimization profiling, or improving the documentation, your help makes **MessageFrame** better for everyone.

Please review our strict development workflow and style guidelines in [CONTRIBUTING.md](CONTRIBUTING.md) before submitting a Pull Request.

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.
