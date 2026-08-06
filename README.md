# MessageFrame

A lightweight C++17 library for structured network messaging: typed key-value
parameters, MessagePack serialization, and binary attachments. No schema files,
no code generation. Add a parameter and serialize it in two lines.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)

## What is this library for

Many telemetry and control systems rely on schema-based messaging frameworks
such as Google Protocol Buffers (Protobuf) or FlatBuffers. They're powerful, but they require
predefined `.proto`/`.fbs` files and a code generation step — which gets in
the way when message structure is decided at runtime rather than fixed at
compile time.

MessageFrame takes a different approach: messages are built dynamically from
key-value parameters, with no schema files and no code generation. A single
message can also carry heavy binary payloads (IQ samples, spectra, raw
arrays) alongside its parameters, all in one packet.

This trades zero-copy access for runtime flexibility. Unlike FlatBuffers,
where data is read directly from the buffer without unpacking, MessageFrame
performs an explicit `deserialize()` step to build its parameter map. That's
the price of having no `.proto` files and no code generation — a deliberate
trade-off, not an oversight.

## Core concept: two-part keys

Instead of designing a custom struct for every device or message type, you
address each parameter with two strings — a **device identifier** and a
**parameter name**:

```cpp
msg.add("sdr_1",     "tx_gain",     msgframe::VALUE(10.0));
msg.add("sdr_1",     "sample_rate", msgframe::VALUE(2'000'000.0));
msg.add("sdr_2",     "rx_gain",     msgframe::VALUE(25.0));
msg.add("sdr_2",     "center_freq", msgframe::VALUE(433'000'000.0));
msg.add("core",      "firmware",    msgframe::VALUE("v1.3.5"));
msg.add("channel_1", "status_ok",   msgframe::VALUE(true));
```

This naturally forms a `device -> parameter -> value` structure inside a
single message. Independent devices or subsystems can contribute parameters
to the same message without knowing about each other, and there's no
per-device struct or serialization code to maintain. Adding a new device or
metric to the stream is just another `.add()` call at runtime.

## Key features

- **Schema-less, but typed.** No `.proto`/`.fbs` files, no external
  compilers in the build pipeline, no generated code. Parameters keep their
  type (`int64_t`, `double`, `bool`, `string`) through `ParameterValue`, and
  the whole API is just `msg.add(...)` / `msg.find(...)`.
- **Three-part layout.** Each message separates a fixed-size **header**
  (routing without parsing the body), small **parameters** addressed by
  `device.parameter`, and heavy binary **attachments** stored as-is.
- **Cache-friendly parameter storage.** Parameters live in a flat
  `std::vector` up to `SMALL_CAPACITY` (128 by default) for allocation-free,
  cache-local access, then transparently switch to a hash map
  (`tsl::robin_map`) beyond that — the API doesn't change either way.
- **Optional sizing hint (`FrameConfig`).** If you know a message will
  exceed `SMALL_CAPACITY`, a hint lets the container start directly in map
  mode, sized for the real count, skipping the fill-then-migrate step.
- **MessagePack wire format.** Serialization produces standard MessagePack,
  so messages can be read by any MessagePack-compatible implementation, not
  just this library.

See [docs/architecture.md](docs/architecture.md) for the `HybridMessageMap`
internals, the full frame layout, and how `FrameConfig` works under the hood.

## Typical use cases

- **Controlling multiple SDR devices at once.** A single TX/RX SDR exposes
  dozens of configuration parameters (channel gain, sample rate, center
  frequency, bandwidth, antenna mode, and so on). Each device is described
  through the same API under a different device key, and everything fits
  into one network message.
- **Collecting telemetry from a fleet of devices.** Temperature, supply
  voltage, connection status, firmware version, error codes — any number of
  metrics from any number of sources, without a fixed schema.
- **Command/control messages.** The same `device.parameter = value`
  structure works for control commands (set frequency, enable channel,
  change mode) and for status reports alike.
- **Shipping raw data alongside metadata.** The `attachments` mechanism lets
  you attach binary blobs — raw IQ samples, a captured spectrum snapshot —
  without routing them through the parameter map.

## Quick start

```cpp
#include <messageframe/MessageFrame.hpp>
#include <iostream>
#include <vector>

msgframe::MessageFrame msg(/*msg_id=*/1001, /*msg_type=*/1, /*src_id=*/50, /*tgt_id=*/99, /*msg_cnt=*/1);

msg.add("sensor_alpha", "voltage",    msgframe::VALUE(12.6));
msg.add("device_core",  "fw_version", msgframe::VALUE("v3.2.1"));

std::vector<uint8_t> buffer;
msg.serialize(buffer);

// Buffer is now a flat byte array, ready to be sent over Network (TCP/UDP), DMA, or IPC
// [Host/Source] ---> ( Network / DMA / IPC ) ---> [Target/Destination]

msgframe::MessageFrame received;
if (received.deserialize(buffer.data(), buffer.size())) {
    if (const auto* val = received.find("device_core", "fw_version")) {
        // Typed access returns std::optional and never throws on a type mismatch
        if (auto as_string = val->tryGetString()) {
            std::cout << "Firmware version: " << *as_string << "\n";
        }
    }
}
```

A full walkthrough — header configuration, attachments, iteration,
`add()`/`set()`/`update()` semantics, `FlatKey` for hot loops, and `clear()`
— is in the [API guide](docs/api-guide.md).

## Installation

```bash
git clone --recursive https://github.com/stubcpp/MessageFrame.git
cd MessageFrame
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

If you cloned without `--recursive`, run
`git submodule update --init --recursive` before building. No system-wide
package manager is required — dependencies (`msgpack-c`, `tsl::robin_map`)
are vendored as Git submodules. See the
[installation guide](docs/installation.md) for the helper scripts,
`FetchContent` integration, and manual source integration.

## Performance

Measured on an Intel Core 7 240H (Ubuntu 22.04, GCC, Release build). Unless
noted otherwise, times cover the full per-message cycle: `add` →
`serialize` → `deserialize`.

| Scenario | Time per message | Throughput | Packed size |
|---|---|---|---|
| 4 parameters | 0.68 us | ~1.47M msgs/sec | 84 bytes |
| 127 parameters (vector-mode ceiling) | 10.41 us | ~96K msgs/sec | 2,075 bytes |
| 150 parameters (hash-map mode) | 22.03 us | ~45K msgs/sec | 2,488 bytes |
| 1,024 parameters, with sizing hint | 173.72 us | ~5.7K msgs/sec | 19,012 bytes |

For a 1024-parameter message, passing a `FrameConfig::initial_reserve`
hint (see [Key features](#key-features) above, or
[architecture.md](docs/architecture.md#sizing-hint-via-frameconfig-optional)
for the details) avoids the vector-to-map migration and measurably reduces
insertion cost:

| Metric (1,024 params/msg) | Without hint | With hint | Change |
|---|---|---|---|
| Parameter insertion (`sum_add`) | 88.14 us | 41.09 us | -53% |
| Total time per message | 219.43 us | 173.72 us | -21% |
| Throughput | 82.63 MB/sec | 104.37 MB/sec | +26% |
| Point lookup (`sum_find`, worst case) | 0.06 us | 0.06 us | unchanged |

Point lookups stay at roughly 60 ns even at 1024 entries, since
`tsl::robin_map` keeps its buckets in a contiguous array rather than
chained nodes. Full results and methodology are in the
[performance benchmarks](docs/performance.md).

## Documentation

- [Architecture & internals](docs/architecture.md) — `HybridMessageMap`, the three-part layout, `FrameConfig`, project structure
- [API guide](docs/api-guide.md) — full usage example, `add()`/`set()`/`update()`, `FlatKey`, SSO, `clear()`
- [Installation guide](docs/installation.md) — all four integration methods
- [Cross-Platform Compatibility Guide](docs/cross_platform_compatibility.md) — Cross-Language Interoperability
- [Performance benchmarks](docs/performance.md) — full results table
- [Guidance for AI assistants](docs/for-ai-assistants.md) — integration rules for LLM-based coding tools

## Contributing

Contributions are welcome — bug fixes, documentation improvements, and new
features alike. See [CONTRIBUTING.md](CONTRIBUTING.md) for the workflow and
guidelines.

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.
