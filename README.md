# MessageFrame

A lightweight C++17 library for structured network messaging: typed key-value 
parameters, MessagePack serialization, and binary attachments. No schema files, 
no code generation. Simple API: add a parameter and serialize in two lines.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)

## What is this library for

Many telemetry and control systems rely on schema-based messaging frameworks
such as `Google Protocol Buffers` or `FlatBuffers`. They're powerful, but they
require predefined `.proto`/`.fbs` files and a code generation step — which
gets in the way when message structure is decided at runtime rather than
fixed at compile time.

**MessageFrame**  takes a different approach: messages are built dynamically
from key-value parameters, with no schema files and no code generation.
A single message can also carry heavy binary payloads (IQ samples, spectra,
raw arrays) alongside its parameters, all in one packet.

## Core concept: two-part keys

Instead of designing a custom struct for every device or message type, you
address each parameter with two strings — a **device identifier** and a
**parameter name**:

```cpp
msg.add("sdr_1", "tx_gain", VALUE(10.0));
msg.add("sdr_1", "sample_rate", VALUE(2'000'000.0));
msg.add("sdr_2", "rx_gain", VALUE(25.0));
msg.add("sdr_2", "center_freq", VALUE(433'000'000.0));
```

This naturally forms a `device -> parameter -> value` structure inside a
single message. Independent devices or subsystems can contribute parameters
to the same message without knowing about each other, and there's no
per-device struct or serialization code to maintain.

## 🚀 Key features
 
- **⚡ Schema-less, but typed.** No `.proto`/`.fbs` files, no external
  compilers in the build pipeline, no generated code. Parameters keep their
  type (`int64_t`, `double`, `bool`, `string`) through `ParameterValue`, and
  the whole API is just `msg.add(...)` / `msg.find(...)`.
- **🔌 Three-part layout.** Each message separates concerns clearly:
  - **Header** — fixed-size, for routing without parsing the full message.
  - **Parameters** — small metrics/commands, addressed by `device.parameter`.
  - **Attachments** — heavy binary payloads, stored and transmitted as-is.
- **🛡️ Cache-friendly parameter storage.** Parameters are kept in a flat
  `std::vector` as long as their count stays at or below `SMALL_CAPACITY`
  (128 by default), avoiding heap allocation and maximizing cache locality
  for the common case. Once that threshold is exceeded, the container
  transparently switches to a hash map (`tsl::robin_map`) — the API doesn't
  change, lookups stay fast at any size.
- **💾 MessagePack wire format.** Serialization produces standard MessagePack,
  so messages can be read by any MessagePack-compatible implementation, not
  just this library.
  
## Typical use cases
 
- **Controlling multiple SDR devices at once.** A single TX/RX SDR exposes
  dozens of configuration parameters (channel gain, sample rate, center
  frequency, bandwidth, antenna mode, and so on). With several SDRs in the
  system, each one is described through the same API under a different
  device key, and everything fits into one network message.
- **Collecting telemetry from a fleet of devices.** Temperature, supply
  voltage, connection status, firmware version, error codes — any number of
  metrics from any number of sources, without a fixed schema.
- **Command/control messages.** The same `device.parameter = value`
  structure works for control commands (set frequency, enable channel,
  change mode) and for status reports alike — symmetric in both directions.
- **Shipping raw data alongside metadata.** The `attachments` mechanism lets
  you attach binary blobs to a message without routing them through the
  parameter map — for example, raw IQ samples or a captured spectrum
  snapshot that needs to travel together with its parameters.

## 🗺️ Internals & Layout
 
```
├── include/
│   └── messageframe/
│       ├── Header.hpp             # Fixed-size message header
│       ├── Value.hpp              # Tagged-union ParameterValue (int64/double/bool/string)
│       ├── HybridMessageMap.hpp   # Vector-to-hash-map container (pImpl facade)
│       ├── Structures.hpp         # Shared types (FlatKey, Attachment)
│       └── MessageFrame.hpp       # Top-level message: header + parameters + attachments
├── src/
│   ├── Header.cpp
│   ├── Value.cpp
│   ├── HybridMessageMap.cpp       # Keeps <tsl/robin_map.h> as a private implementation detail
│   └── MessageFrame.cpp
├── third_party/                   # Vendored header-only dependencies
│   ├── robin_map/                 # tsl::robin_map
│   └── msgpack/                   # MessagePack serialization/deserialization
├── examples/
│   └── basic_usage.cpp            # Minimal demonstration of the API
├── benchmarks/
│   └── benchmark.cpp              # Parameterized performance benchmark (--iterations, --params)
├── tests/
│   ├── test_framework.hpp         # Zero-dependency test harness
│   ├── test_hybrid_map.cpp        # HybridMessageMap correctness tests
│   └── test_messageframe_proxy.cpp # MessageFrame proxy-method tests
└── CMakeLists.txt
└── run_benchmark.sh
└── run_benchmark.bat
```
 
## Performance Benchmarks
 
*Tested on: Intel Core i7-4702MQ @ 2.20GHz, Windows 10 (x64 Release, MSVC),
power plan set to maximum performance. Run via
`benchmarks/benchmark.cpp --iterations 10000 --params N`; figures below are
typical results, not best-case outliers — run-to-run variance on this
hardware is roughly ±10%.*
 
### Scenario A: small frame (4 parameters)
 
Header + 4 parameters, no attachment.
 
|              Metric                 |              Value                 |
|-------------------------------------|------------------------------------|
| Avg time per message                | 2.43 us                            |
| Throughput                          | 412,067 messages/sec (32.6 MB/sec) |
| Avg packed size                     | 82 bytes                           |
| `add` / `serialize` / `deserialize` | 0.34 us / 0.60 us / 1.22 us        |
 
### Scenario B: large frame (150 parameters)
 
Header + 150 parameters — past `SMALL_CAPACITY`, so the container has
switched to its hash-map mode.
 
|              Metric                 |              Value                |
|-------------------------------------|-----------------------------------|
| Avg time per message                | 65.86 us                          |
| Throughput                          | 15,183 messages/sec (36.0 MB/sec) |
| Avg packed size                     | 2,486 bytes                       |
| `add` / `serialize` / `deserialize` | 21.03 us / 14.70 us / 23.94 us    |
 
## 🛠️ Installation & Build Guide
 
This library is self-contained and uses Git submodules for its two
dependencies (`msgpack-c` and `tsl::robin_map`), so no system-wide package
manager (and no Boost) is required.
 
### 0. Prerequisites
 
To build the library you need:
 
- **Git** — to clone the repository and fetch the submodules
  (`msgpack-c`, `tsl::robin_map`). Without it, `third_party/` stays empty
  and the build fails.
- **CMake 3.14 or newer.**
- **A C++17 compiler:**
  - *Windows* — Visual Studio 2019 or newer, with the "Desktop development
    with C++" workload (this also bundles a compatible CMake, which the
    `.bat` script can find automatically — see below).
  - *Linux* — GCC 7+ or Clang 5+ (e.g. the `build-essential` package).
  - *macOS* — Xcode Command Line Tools (`xcode-select --install`).
### 1. Cloning the repository
 
```bash
git clone --recursive https://github.com/stubcpp/MessageFrame.git
cd MessageFrame
```
 
If you already cloned without `--recursive`, fetch the submodules separately:
 
```bash
git submodule update --init --recursive
```
 
### 2. Building
 
Always build in **Release**: optimizations (`/O2` on MSVC, `-O3` on
GCC/Clang) matter here, especially for the benchmark — a Debug build adds
heavy STL iterator-validation overhead that skews timing results
significantly.
 
#### Using the helper scripts
 
`run_benchmark.bat` (Windows) and `run_benchmark.sh` (Linux/macOS) are
convenience scripts that automate the whole setup-and-run sequence in one
step — useful if you just want to see the benchmark working without typing
out CMake commands by hand. Each script:
 
1. Locates CMake — on Windows, it first checks whether Visual Studio ships
   its own bundled CMake (via `vswhere.exe`) before falling back to a
   system-wide install; on Linux/macOS, it checks that `cmake` is on `PATH`
   and tells you how to install it if not.
2. Fetches Git submodules (`git submodule update --init --recursive`), so
   you don't need to remember this step yourself.
3. Removes any previous build directory, so every run starts from a clean
   CMake configuration — this avoids stale-cache issues (for example, a
   leftover Debug configuration from an earlier run on a multi-config
   generator like Visual Studio).
4. Configures and builds in Release, with only the benchmark target
   enabled (`examples`/`tests` are skipped to keep the build fast), using
   all available CPU cores.
5. Runs the resulting `messageframe_benchmark` binary.
**Windows (Visual Studio / MSVC):**
 
```cmd
run_benchmark.bat
```
 
Or build manually:
 
```cmd
cmake -B build
cmake --build build --config Release
```
 
**Linux / macOS (GCC / Clang):**
 
```bash
chmod +x run_benchmark.sh
./run_benchmark.sh
```
 
Or build manually:
 
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j$(nproc)
```
 
Any arguments you pass to either script are forwarded straight to the
benchmark binary:
 
```bash
./run_benchmark.sh --params 150 --iterations 50000
```
```cmd
run_benchmark.bat --params 150 --iterations 50000
```
 
### 3. Running things manually
 
If you built the full project (examples + benchmarks + tests, the default),
the resulting binaries are:
 
```bash
# Windows
.\build\Release\messageframe_example.exe
.\build\Release\messageframe_benchmark.exe --iterations 50000 --params 150
.\build\Release\messageframe_tests.exe
 
# Linux / macOS
./build/messageframe_example
./build/messageframe_benchmark --iterations 50000 --params 150
./build/messageframe_tests
```
 
Each of the three is optional and can be disabled at configure time, e.g.
`cmake -B build -DMSGFRAME_BUILD_TESTS=OFF`.

## Usage Example
 
```cpp
#include <messageframe/MessageFrame.hpp>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string_view>
 
// Strongly-typed message tags — use your own enums instead of raw integers.
 
// MyMsgId is the message "catalog" for your system — every distinct kind of
// message or command your application sends gets its own entry here. This is
// what a receiver switches on to decide *what to do* with an incoming message
// (e.g. "this is a telemetry packet" vs "this is a command to execute").
// Think of it as your protocol's dispatch table, not just a label.
enum class MyMsgId : int32_t {
    TELEMETRY_PACKET = 1001,
    COMMAND_PACKET   = 1002
};
 
// MyMsgType is a lightweight, orthogonal classification tag — it doesn't say
// *what* the message is, only *how* it should be treated (priority, urgency,
// delivery semantics). The same MsgId can show up with different MsgTypes:
// a TELEMETRY_PACKET might be PERIODIC most of the time, but CRITICAL when a
// sensor crosses a threshold. Extend this freely with values like ALARM,
// COMMAND, ACK, or whatever distinctions your routing/logging logic needs.
enum class MyMsgType : int32_t {
    PERIODIC = 1,
    CRITICAL = 2
};
 
// A simple callback used to demonstrate fast, allocation-free iteration
void printParam(std::string_view flat_key, const msgframe::ParameterValue& val, void* /*user_data*/) {
    std::cout << "  [Iterate] " << flat_key << " = " << val.toString() << "\n";
}
 
int main() {
    // ----------------------------------------------------------------
    // 1. Create a message and configure its header
    // ----------------------------------------------------------------
    // The templated constructor accepts any custom enum or integer type
    // for message ID / message type — no need to cast to int32_t yourself.
    //   args: msg_id, msg_type, source_id, target_id, message_counter,
    //         proto_version (default = 1), msg_flags (default = 0)
    msgframe::MessageFrame msg(
        MyMsgId::TELEMETRY_PACKET, // user-defined enum (cast to int32_t internally)
        MyMsgType::CRITICAL,       // user-defined enum (cast to int32_t internally)
        /*source_id=*/50,          // uint32_t
        /*target_id=*/99,          // uint32_t
        /*msg_cnt=*/1,             // uint64_t
        /*proto_version=*/1,       // uint16_t, default = 1
        /*msg_flags=*/0x0001);     // uint16_t, default = 0
 
    // Every field is also reachable on the fly after construction —
    // useful when a message is reused or re-purposed before sending.
    msg.header().setFlags(0xAA00);
    msg.header().setMessageId(MyMsgId::COMMAND_PACKET);
    msg.header().setMessageType(MyMsgType::PERIODIC);
    msg.header().updateTimestamp(); // refresh to "now" right before transmission
 
    // ----------------------------------------------------------------
    // 2. Add parameters using the two-key API (device, parameter, value)
    // ----------------------------------------------------------------
    msg.add("sensor_alpha", "voltage",     msgframe::VALUE(12.6));
    msg.add("sensor_alpha", "status_ok",   msgframe::VALUE(true));
    msg.add("device_core",  "fw_version",  msgframe::VALUE("v3.2.1"));
    msg.add("device_core",  "error_codes", msgframe::VALUE(-5));
 
    // ----------------------------------------------------------------
    // 3. Attach a raw binary payload (e.g. IQ samples, a spectrum snapshot)
    //    Attachments bypass the parameter map entirely — no copying
    //    your bulk data through the key/value store.
    // ----------------------------------------------------------------
    std::vector<uint8_t> raw_iq_data = { 0x01, 0x02, 0x03, 0x04, 0x05, 0xAA, 0xBB, 0xCC };
    msg.add_attachment("raw_iq_stream", std::move(raw_iq_data));
 
    std::cout << "Header Timestamp:  " << msg.header().getTimestamp() << " ms\n";
    std::cout << "Header MsgID:      " << msg.header().getMessageIdRaw() << "\n";
    std::cout << "Header Version:    " << msg.header().getVersion() << "\n";
    std::cout << "Header Flags:      0x" << std::hex << msg.header().getFlags() << std::dec << "\n";
    std::cout << "Total parameters:  " << msg.parameters_size() << "\n";
    std::cout << "Total attachments: " << msg.get_attachments().size() << "\n\n";
 
    // ----------------------------------------------------------------
    // 4. Look up a single value without allocating, or iterate over all of them
    // ----------------------------------------------------------------
    if (const auto* val = msg.find("sensor_alpha", "voltage")) {
        std::cout << "Found sensor_alpha.voltage: " << val->toString() << "\n";
    }
    msg.iterate_parameters(printParam, nullptr);
 
    // ----------------------------------------------------------------
    // 5. Transport-agnostic serialization — write straight into a buffer
    //    ready to be sent over any socket, queue, or shared-memory channel
    // ----------------------------------------------------------------
    std::vector<uint8_t> send_buffer;
    msg.serialize(send_buffer);
 
    // ----------------------------------------------------------------
    // 6. On the receiving end: decode in place from the raw bytes
    // ----------------------------------------------------------------
    msgframe::MessageFrame received;
    if (received.deserialize(send_buffer.data(), send_buffer.size())) {
        if (received.header().getMessageType<MyMsgType>() == MyMsgType::PERIODIC) {
            std::cout << "\nDecoded message type: PERIODIC\n";
        }
        if (const auto* val = received.find("sensor_alpha", "voltage")) {
            std::cout << "Decoded sensor_alpha.voltage: " << val->toString() << "\n";
        }
    }
 
    return 0;
}
```
## 💡 API Usage & Performance Guidelines
 
The insertion API is split into three modes — `add()`, `set()`, and
`update()` — each with a different cost/safety trade-off. Picking the right
one for a given call site keeps hot paths allocation-free where it matters.
 
|                        | `add()` / `add_flat()`                 |
|------------------------|----------------------------------------|
| **Semantics**          | Append, no duplicate check             |
| **Complexity**         | O(1)                                   |
| **On missing key**     | Inserts                                |
| **On existing key**    | Duplicate (Release) / `assert` (Debug) |

|                        |  `set()` / `set_flat()`                |
|------------------------|----------------------------------------|
| **Semantics**          |  Upsert (insert or overwrite)          |
| **Complexity**         |  O(N) vector-mode, O(1) map-mode       |
| **On missing key**     |  Inserts                               |
| **On existing key**    |  Overwrites                            |

|                        |  `update()` / `update_flat()`          |
|------------------------|----------------------------------------|
| **Semantics**          |  Strict in-place edit only             |
| **Complexity**         |  O(N) vector-mode, O(1) map-mode       |
| **On missing key**     |  Returns `false`, no change            |
| **On existing key**    |  Overwrites                            |


### `add()` / `add_flat()` — append-only, no duplicate check
 
In vector mode this is a plain `push_back()`; in map mode, an `emplace()`.
Use it for high-frequency streams where you assemble a frame from scratch
in a deterministic loop and know each key is unique.
 
**Be careful:** a duplicate key bypasses the check in Release builds (the
vector-mode path doesn't scan for existing entries, by design, to stay
O(1)) — `find()` will then return whichever entry came first, silently. In
Debug builds (`#ifndef NDEBUG`), an `assert()` catches this during
development.
 
### `set()` / `set_flat()` — upsert
 
Looks for the key first; if found, overwrites it in place, otherwise
inserts. Use it when parameters can arrive out of order, or when multiple
subsystems might write to the same `device.parameter` within one frame
cycle. In vector mode this costs an O(N) linear scan (`std::find_if`)
before the eventual insert; in map mode it's a single `insert_or_assign()`.
 
### `update()` / `update_flat()` — strict in-place edit
 
Modifies an existing entry and never grows the container. Useful for
pre-populated frame templates, where a downstream filter stage should only
be allowed to adjust fields that already exist — `update()` returns
`false` (and leaves the container untouched) if the key isn't there,
instead of silently creating it.
 
### Keep `device`/`parameter` keys short
 
Internally, every key ends up in a `std::string` (the combined
`device.parameter` flat key). Whether that allocates on the heap is
entirely up to you, the caller: most standard library implementations use
small string optimization (SSO), storing strings inline — inside the
string object itself, no heap allocation — as long as the string's total
length stays under roughly 15–23 bytes (the exact threshold depends on the
standard library). Keep your `device` and `parameter` names short (e.g.
`"sdr_1"` / `"tx_gain"` rather than long descriptive sentences) and most
key construction stays allocation-free; long, verbose keys will allocate.
 
### Methods with the _flat suffix (add_flat(), set_flat(), update_flat())

Methods with the `_flat` suffix (`add_flat()`, `set_flat()`, `update_flat()`) are
variants of the main methods that accept an already concatenated key as 
a single string `("device.parameter")`, rather than two separate strings 
(device, parameter).
The usual add(device, param, val) internally concatenates the two strings 
into a single flat_key `(device + "." + param)` before passing it on — and it 
is at this step that temporary allocation occurs if the result does not fit 
into SSO. add_flat(flat_key, val) skips this concatenation step: if the caller 
already has a ready-made string (for example, it is stored somewhere as a constant, 
or came from the network already in the `"device.parameter"` format), you can pass 
it directly, without unnecessary concatenation.
That is, _flat-variants are not a separate functionality, but the same method, 
just one step closer to the internal implementation, for cases when the user wants 
to manage the key himself and avoid unnecessary string concatenation on the hot path.


## 🤝 Contributing

Contributions are welcome! 
Whether it's fixing a bug, improving documentation, or adding new features, 
your help makes **MessageFrame** better for everyone.

To contribute:

1. **Fork** the repository and create your branch from `main`.
2. **Make your changes** — keep commits focused and clear.
3. **Run tests and benchmarks** to ensure nothing breaks.
4. **Submit a pull request** with a clear description of your changes.

### Guidelines
- Follow the existing coding style (C++17, modern CMake).
- Keep public APIs minimal and consistent.
- Add unit tests for new functionality in the `tests/` directory.
- Update documentation (README, comments) if behavior changes.

If you’re unsure about a change, feel free to open an **issue** first to discuss it. 
Even small contributions like typo fixes or clarifying comments are appreciated!


## 📜 License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.


