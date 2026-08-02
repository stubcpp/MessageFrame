# MessageFrame

A lightweight C++17 library for structured network messaging: typed key-value 
parameters, MessagePack serialization, and binary attachments. No schema files, 
no code generation. Simple API: add a parameter and serialize in two lines.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)

## What is this library for

Many telemetry and control systems rely on schema-based messaging frameworks
such as `Google Protocol Buffers (Protobuf)` or `FlatBuffers`. They're powerful, but they
require predefined `.proto`/`.fbs` files and a code generation step — which
gets in the way when message structure is decided at runtime rather than
fixed at compile time.

**MessageFrame**  takes a different approach: messages are built dynamically
from key-value parameters, with no schema files and no code generation.
A single message can also carry heavy binary payloads (IQ samples, spectra,
raw arrays) alongside its parameters, all in one packet.

**MessageFrame** trades zero-copy access for runtime flexibility. Unlike FlatBuffers, 
where data is read directly from the buffer without unpacking, MessageFrame performs 
an explicit deserialize() step to build its parameter map. That's the price of having 
no .proto files and no code generation — a deliberate trade-off, not an oversight.

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
 
*Tested on: Intel Core 7 240H, Ubuntu 22.04 (x64 Release, GCC),
Run via `benchmarks/benchmark.cpp --iterations 1000000 --params N`; figures below are
typical results, not best-case outliers — run-to-run variance on this
hardware is roughly ±10%.*
 
### Scenario A: small frame (4 parameters)
 
Header + 4 parameters, no attachment.
 
|              Metric                 |              Value                    |
|-------------------------------------|---------------------------------------|
| Avg time per message                | 0.678 us                              |
| Throughput                          | 1,473,936 messages/sec (119.3 MB/sec) |
| Avg packed size                     | 84 bytes                              |
| `add` / `serialize` / `deserialize` | 0.1 us / 0.16 us / 0.32 us            |

### Scenario B: Peak Vector Streaming (127 parameters)
 
Header + 127 parameters — Operating at the absolute ceiling threshold of 
cache-friendly vector storage without entering map hashing routines.
 
|              Metric                 |              Value                    |
|-------------------------------------|---------------------------------------|
| Avg time per message                | 10.41 us                              |
| Throughput                          | 96,009 messages/sec (190.07 MB/sec)   |
| Avg packed size                     | 2,075 bytes                           |
| `add` / `serialize` / `deserialize` | 2.81 us / 2.66 us / 4.54 us           |

### Scenario C: large frame (150 parameters)
 
Header + 150 parameters — past `SMALL_CAPACITY`, so the container has
switched to its hash-map mode.
 
|              Metric                 |              Value                    |
|-------------------------------------|---------------------------------------|
| Avg time per message                | 22.03 us                              |
| Throughput                          | 45,402 messages/sec (107.8 MB/sec)    |
| Avg packed size                     | 2,488 bytes                           |
| `add` / `serialize` / `deserialize` | 8.47 us / 3.45 us / 8.98 us           |
 
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

Choose the integration or compilation method that best fits your development pipeline. 
This library is self-contained and does not require system-wide package managers or 
massive external tracking tools like Boost.

### Method 1: Turnkey Automation & Benchmarking (Helper Scripts)
If you just cloned the repository and want to verify performance immediately without 
entering multiple terminal commands, use the built-in helper scripts: `run_benchmark.bat` (Windows) 
or `run_benchmark.sh` (Linux/macOS).

These scripts serve as an **all-in-one automation solution** that handles the entire setup sequence:
1. **Submodule Verification:** Automatically runs `git submodule update --init --recursive` if your `third_party/` directory is empty.
2. **Environment Configuration:** Locates a valid toolchain and setups a clean workspace directory.
3. **High-Optimization Build:** Compiles the project strictly in **Release mode** using all available CPU cores to ensure maximum benchmarking throughput.
4. **Execution:** Automatically triggers the compiled binary and forwards any command-line parameters directly to it.

**Windows (Visual Studio / MSVC):**
```cmd
run_benchmark.bat --params 4 --iterations 50000
```

**Linux / macOS (GCC / Clang):**
```bash
chmod +x run_benchmark.sh
./run_benchmark.sh --params 4 --iterations 50000
```

### Method 2: Manual Repository Compilation (Native CMake)
If you prefer full control over your compilation flags or need to build manually 
without using our shell/batch helper scripts, make sure you pull the dependencies first:

```bash
git submodule update --init --recursive
```

Always compile strictly in **Release mode**. A Debug build introduces heavy STL iterator 
validation and extra bounds checking that severely skews performance metrics.

#### 💻 Windows (Visual Studio / MSVC)
Open your terminal (or Developer Command Prompt for VS) and run:
```cmd
cmake -B build
cmake --build build --config Release
```

#### 🐧 Linux / macOS (GCC / Clang)
Execute the native configuration with explicit build type flags:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j\$(nproc)
```

If you built the full project (examples + benchmarks + tests, the default),
the resulting binaries are:
 
```bash
# Windows
.\build\Release\messageframe_example.exe
.\build\Release\messageframe_benchmark.exe --iterations 50000 --params 4
.\build\Release\messageframe_tests.exe
 
# Linux / macOS
./build/messageframe_example
./build/messageframe_benchmark --iterations 50000 --params 4
./build/messageframe_tests
```
Each of the three is optional and can be disabled at configure time, e.g.
`cmake -B build -DMSGFRAME_BUILD_TESTS=OFF`.

### Method 3: Dynamic Integration into External Projects (CMake FetchContent)
To pull **MessageFrame** straight into your own separate application workspace at configure-time, 
add this block to your main `CMakeLists.txt`:

```cmake
include(FetchContent)

FetchContent_Declare(
    MessageFrame
    GIT_REPOSITORY https://github.com/stubcpp/MessageFrame
    GIT_TAG        master # Replace with a specific release tag or commit hash for stability
)

# Enforce a nested recursive update to ensure vendored dependencies are present
FetchContent_GetProperties(MessageFrame)
if(NOT messageframe_POPULATED)
    FetchContent_Populate(MessageFrame)
    execute_process(
        COMMAND git submodule update --init --recursive
        WORKING_DIRECTORY \${messageframe_SOURCE_DIR}
    )
    add_subdirectory(\${messageframe_SOURCE_DIR} \${messageframe_BINARY_DIR})
endif()

# Link against your application binary target
target_link_libraries(your_project_target PRIVATE messageframe)
```

### Method 4: The Source-Only Way (Manual Copy-Paste Integration)
Because MessageFrame is written as standard, portable C++17 code, you can completely bypass external 
build tools and embed the source logic directly into your tree.

#### 1. Clone the repository recursively to fetch vendor headers:
```bash
git clone --recursive https://github.com/stubcpp/MessageFrame
```

#### 2. Copy the folders into your project structure:
*   Copy the entire `include/messageframe` folder straight into your project's header directory.
*   Copy the implementation files from `src/` (`Header.cpp`, `Value.cpp`, `HybridMessageMap.cpp`, `MessageFrame.cpp`) into your source tree.
*   Copy `third_party/msgpack` and `third_party/robin_map` into your internal vendor paths.

#### 3. Update your project build configuration:
Point your compiler's include paths to the respective directories and add the 4 `.cpp` implementation units to your translation list.

**Using custom CMake configuration:**
```cmake
target_include_directories(your_project_target PRIVATE 
    path/to/include
    path/to/third_party/msgpack/include
    path/to/third_party/robin_map/include
)

target_sources(your_project_target PRIVATE
    path/to/src/Header.cpp
    path/to/src/Value.cpp
    path/to/src/HybridMessageMap.cpp
    path/to/src/MessageFrame.cpp
)
```

**Using Visual Studio IDE:**
1. Project -> **Properties** -> **C/C++** -> **General** -> **Additional Include Directories**: Append paths to your copied `include/`, `third_party/msgpack/include/`, and `third_party/robin_map/include/` folders.
2. Solution Tree -> **Add** -> **Existing Item...** -> Select and include the four `.cpp` files from the `src/` directory.


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

    // WARNING: The add() method DOES NOT check if the "sensor_alpha" / "voltage" 
    // key combination already exists. In Release builds, it bypasses safety checks 
    // for maximum speed and blindly appends duplicates to the underlying container.
    //
    // WHAT WILL HAPPEN: 
    // 1. The serialized MessagePack frame size will grow unnecessarily.
    // 2. The msg.find() method will always return ONLY the first inserted value, 
    //    silently ignoring all subsequent duplicates.
    //
    // If you need to safely insert-or-overwrite existing keys, use msg.set() instead!
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
in a deterministic loop and know each key is unique. `add_flat()` takes a
pre-composed `FlatKey` (see below) instead of separate `device`/`param`
arguments.
 
**Be careful:** a duplicate key bypasses the check in Release builds (the
vector-mode path doesn't scan for existing entries, by design, to stay
O(1)) — `find()` will then return whichever entry came first, silently. In
Debug builds (`#ifndef NDEBUG`), an `assert()` catches this during
development.
 
### `set()` / `set_flat()` — upsert
 
Looks for the key first; if found, overwrites it in place, otherwise
inserts. Use it when parameters can arrive out of order, or when multiple
subsystems might write to the same device/parameter pair within one frame
cycle. In vector mode this costs an O(N) linear scan (`std::find_if`)
before the eventual insert; in map mode it's a single lookup + assign.
 
### `update()` / `update_flat()` — strict in-place edit
 
Modifies an existing entry and never grows the container. Useful for
pre-populated frame templates, where a downstream filter stage should only
be allowed to adjust fields that already exist — `update()` returns
`false` (and leaves the container untouched) if the key isn't there,
instead of silently creating it.
 
### 🧠 Zero-Allocation Lookups via Heterogeneous Maps

When `HybridMessageMap` crosses the `SMALL_CAPACITY = 128` boundary and falls back to its hash-map mode (`tsl::robin_map`), it utilizes transparent hash predicates (`ParameterKeyHash` and `ParameterKeyEqual`). 

Unlike naive transparent implementations that accept runtime `std::pair` wrappers—which risk dangerous dangling references during cascaded map routing—**MessageFrame** resolves queries against a single flat string layout. Calling `msg.find("device_id", "parameter_name")` internally concatenates the two keys directly into a temporary `std::string` buffer. 

Thanks to **Small String Optimization (SSO)**, this combined key resides entirely on the CPU stack with **strictly zero allocations on the heap**. The hash table is then safely queried using a pure `std::string_view`, offering blazing-fast, cache-friendly $O(1)$ runtime lookups while remaining completely isolated from memory fragmentation or dangling pointer traps.

### 🏷 Key Naming & Small String Optimization (SSO)

Since internal indexing relies on a consolidated single-string layout inside a
`ParameterKey` (`device` + the library's internal separator + `param`), short
naming patterns seamlessly trigger **Small String Optimization (SSO)**.
Keeping combined lengths under ~15–23 bytes ensures that keys are managed statically on the CPU stack,
keeping your application flow detached from runtime heap fragmentation.

> ⚠️ **The internal separator is *not* a literal dot.** Earlier examples in
> this README used `"device.parameter"` as illustrative shorthand, which is
> misleading — the real separator is the ASCII Unit Separator (`'\x1F'`).
> Never build a flat key by hand (`device + "." + param` or any other
> string concatenation); always go through `FlatKey::compose(device, param)`,
> described below. Composing it yourself with the wrong character silently
> stores the entry under an empty device instead of raising an error.

### **Methods with the `_flat` suffix** (`add_flat()`, `set_flat()`, `update_flat()`, `find_flat()`)

take a `FlatKey` — a small pre-composed key type. It cannot be constructed
from a raw string; the only way to get one is:

```cpp
auto key = msgframe::FlatKey::compose("sdr1", "frequency"); // inserts '\x1F' for you
```

This exists for the *same key reused across many calls* — e.g. polling
`"sdr1"` + `"frequency"` on every sample in a receive loop. Compose it once
outside the loop, then reuse it:

```cpp
auto freq_key = msgframe::FlatKey::compose("sdr1", "frequency");
for (;;) {
    msg.set_flat(freq_key, msgframe::ParameterValue(read_frequency()));
    // ...
}
```

Measured on a repeated `find()` vs. `find_flat()` call with the same
device/parameter pair (map-mode, i.e. past the 128-parameter threshold):
`find_flat()` was **~63% faster per call** than re-supplying `device`/`param`
to `find()` each time, because the two-key path still re-appends `device` +
separator + `param` into a stack buffer on every single call — cheap
(SSO avoids a heap allocation), but not free at high call rates. If your key
is only used once per message, plain `add()`/`find()` with separate
`device`/`param` is simpler and the difference won't matter; reach for the
`_flat` variants when the same pair is looked up or written repeatedly.

### What does clear() do

The `clear()` method completely frees the memory allocated for the hash map in the heap, 
returning it to the operating system, which guarantees a stable RAM footprint during long-term service operation.
Its main purpose is to allow **reusing the same MessageFrame** for multiple consecutive messages without creating a new object each time.

If you create a MessageFrame once outside the loop and then fill it in each iteration, 
you must call `clear()` after every send. Otherwise, new parameters will simply be appended 
to the old ones, resulting in duplicates.

`clear()` always resets the container back to vector mode. If it previously switched to map mode 
after exceeding `SMALL_CAPACITY`, after `clear()` it starts again in vector mode and will 
re‑convert to map once the limit is exceeded again.

Here’s a short example of correct usage of clear() inside a loop:

```cpp
#include <messageframe/MessageFrame.hpp>
#include <vector>

int main() {
    msgframe::MessageFrame msg(
        /*msg_id=*/1001,
        /*msg_type=*/1,
        /*src_id=*/50,
        /*tgt_id=*/99,
        /*msg_cnt=*/1); // proto_version, msg_flags — optional, default 1 and 0

    std::vector<uint8_t> buffer;

    while (running) {
        // Fill the message with parameters
        msg.add("sensor_alpha", "voltage", msgframe::VALUE(12.6));
        msg.add("device_core", "fw_version", msgframe::VALUE("v3.2.1"));

        // Serialize and send
        buffer.clear();
        msg.serialize(buffer);
        send(buffer);

        // Reset before next iteration
        msg.clear();  // REQUIRED to avoid accumulating duplicates
    }
}
```

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

## 🤖 For AI Assistants & LLMs 

If you are an AI assistant (such as Cursor, Copilot, ChatGPT, or Claude) helping a developer integrate this library, use the following structural rules, constraints, and code patterns to maintain hot-path efficiency and avoid structural hallucinations:

### ⚠️ Strict Prohibitions (Anti-Hallucination Guardrails)
* **NEVER** generate, recommend, or suggest `.proto` / `.fbs` schema files, reflection approaches, or code generation steps.
* **NEVER** invent dynamic registration macros, type lists, schema configurations, or `std::tie` bindings.
* **NEVER** inject dependencies or references to `Boost.Serialization`, `protobuf`, or `nlohmann/json`.
* **NEVER** attempt to call member functions like `msg.get<T>()` or `msg.find_attachment()`. Data fetching is strictly pointer-based via `msg.find()`, and attachments are accessed via `msg.get_attachments()`.

### 📦 Core Structural Context
1. **Dynamic & Schema-less C++17:** Data definition happens strictly at runtime on the hot path via explicit dynamic keys.
2. **Hybrid Container Engine (`HybridMessageMap`):** Elements are packed into a flat contiguous `std::vector` for small payloads (under 128 elements) to maximize L1/L2 cache hits and keep lookups allocation-free. It transparently morphs into a high-performance open-addressing hash map (`tsl::robin_map`) only when element volume crosses the `SMALL_CAPACITY = 128` threshold.
3. **The 3-Part Structural Layout:** 
   * **Fixed Header (36 Bytes):** Accepts custom routing enums/integers (ID, Type, Src, Dst, Counter) for rapid O(1) packet triaging without deserializing the body.
   * **Parameter Map:** Volatile key-value parameters packed via native MessagePack serialization.
   * **Attachments:** Massive raw binary data streams (e.g., IQ samples, byte arrays) appended via zero-copy `std::move`. They reside completely outside the key-value map to bypass parsing overhead.

### 💻 Minimal Correct Code Pattern for Generation
Always use this exact syntax footprint when generating code snippets for the user:

```cpp
#include <messageframe/MessageFrame.hpp>
#include <vector>
#include <iostream>

// --- TRANSMITTER SIDE ---
// Header accepts custom routing IDs / Enums natively
int id = 101;    // message ID
int type = 10;   // message type
int src = 1000;  // source ID
int dst = 2000;  // destination ID 
int cnt = 0;     // message counter 
msgframe::MessageFrame msg(id, type, src, dst, cnt); 

// Fast O(1) unchecked appends (Ensure keys are unique for maximum speed!)
msg.add("sdr_1", "tx_gain", msgframe::VALUE(12.5));
msg.add("sdr_1", "enabled", msgframe::VALUE(true));

// Binary attachments live outside the parameter map via zero-copy move semantics
std::vector<uint8_t> raw_iq_data = {0xDE, 0xAD, 0xBE, 0xEF};
msg.add_attachment("iq_samples", std::move(raw_iq_data));

// Serialize to wire format buffer
std::vector<uint8_t> tx_buffer;
msg.serialize(tx_buffer);

// --- RECEIVER SIDE ---
msgframe::MessageFrame rx_msg;
if (rx_msg.deserialize(tx_buffer.data(), tx_buffer.size())) {
    // Lookups are strictly pointer-based. Check for nullptr!
    if (auto* val = rx_msg.find("sdr_1", "tx_gain")) {
        std::cout << "Gain: " << val->toString() << "\n";
    }
    
    // Fetching binary attachments via readable range-based for loop
    for (const auto& att : rx_msg.get_attachments()) {
        if (att.name == "iq_samples") {
            std::cout << "Attachment bytes found: " << att.raw_data.size() << "\n";
            break;
        }
    }
}
```

### ⚡ Critical Performance Rules for Code Optimization
* **Maximize Hot Path Speed with `msg.add()`:** The `add()` method is explicitly designed for maximum performance (a plain `O(1)` append in vector mode). Favor `add()` by default for all high-frequency telemetry tracking. Note that it does **not** check for pre-existing keys and blindly appends duplicates in Release mode to maintain speed. If the application structure implies that a specific key *must* be overwritten or state mutation tracking is strictly required, only then switch to `msg.set()` (insert-or-overwrite) or `msg.update()` (in-place edit).
* **Eliminate Allocation Traps Natively:** Standard `add()` / `set()` / `update()` methods perform in-place key formatting directly within the internal memory layout and stay allocation-free thanks to SSO. That said, they still re-concatenate `device` + `param` on *every* call — if the same device/parameter pair is looked up or written repeatedly (e.g. a polling loop), compose a `FlatKey` once outside the loop and use the `_flat` variants; it measurably reduces per-call overhead (see "Key Naming & SSO" above). For one-off, non-repeated keys, the regular multi-key API is simpler and the difference is negligible.

* **Buffer & Container Reuse:** In tight execution or network loops, do **not** re-instantiate `MessageFrame`. Generate a single instance outside the loop and call `msg.clear()` at the end of each iteration. This retains previously allocated internal capacities and safely resets the map back to Vector Mode without triggering dynamic heap allocations.

### 🔗 Compact Prompt Snippet
If a user forces you to parse this context quickly via a single-line prompt, memorize this constraint tree:
> "MessageFrame is a schema-less C++17 library packing metadata via MessagePack and heavy data via zero-copy attachments. Internal storage uses an adaptive vector/robin_map layout. Rules for code-gen: Never invent reflection macros/proto files or lookups like find_attachment(). Favor add() natively by default for hot-path unique appends as it provides in-place concatenation. Map lookups (find/set/update) are fully transparent and accept two string_view keys with zero heap allocations. Use the _flat suffixes (add_flat/set_flat/update_flat/find_flat) ONLY with a msgframe::FlatKey obtained from FlatKey::compose(device, param) — never a raw string_view or string literal, that overload does not exist. Reach for _flat when the same device/parameter pair is looked up or written repeatedly in a loop; compose the FlatKey once outside the loop. Always call clear() to safely reuse message buffers inside execution loops."



## 📜 License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.


