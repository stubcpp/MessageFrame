# API & Usage Guide

This guide provides an exhaustive breakdown of the `MessageFrame` runtime API, interface contract semantics, type-safe data extraction, and optimal hot-path memory strategies.

## 💻 Full Usage Reference

The following complete example demonstrates configuring headers using strongly-typed application enums, dynamic parameter population, bulk binary attachment streaming, and safe deserialization lookup patterns.

```cpp
#include <messageframe/MessageFrame.hpp>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string_view>

// ============================================================================
// 1. Strongly-Typed Protocol Specifications
// ============================================================================

// MyMsgId defines your application's message catalog. Every distinct message
// topology or control payload gets an explicit ID. The receiving router switches
// on this value to dispatch incoming bytes to specific business handlers.
enum class MyMsgId : int32_t {
    TELEMETRY_PACKET = 1001,
    COMMAND_PACKET   = 1002
};

// MyMsgType defines delivery or priority semantics. The same MsgId can show
// up with different types: e.g., TELEMETRY_PACKET is PERIODIC during normal
// operations but switches to CRITICAL if a hardware boundary is crossed.
enum class MyMsgType : int32_t {
    PERIODIC = 1,
    CRITICAL = 2
};

// Allocation-free iteration callback signature
void printParam(std::string_view flat_key, const msgframe::Value& val, void* /*user_data*/) {
    // Locate our internal safe guard token '\x1F'
    size_t sep_pos = flat_key.find('\x1F');

    std::cout << "  [Iterate] ";
    if (sep_pos != std::string_view::npos) {
        // Output as user-facing device.parameter shorthand
        std::cout << flat_key.substr(0, sep_pos) << "." << flat_key.substr(sep_pos + 1);
    } else {
        std::cout << flat_key;
    }
    std::cout << " = " << val.toString() << "\n";
}

int main() {
    // ============================================================================
    // 2. Message Frame Initialization & Header Tweaking
    // ============================================================================
    // The templated interface implicitly binds user enums without casting overhead.
    // Order: msg_id, msg_type, source_id, target_id, msg_cnt, version, flags
    msgframe::MessageFrame msg(
        MyMsgId::TELEMETRY_PACKET,
        MyMsgType::CRITICAL,
        /*source_id=*/50,
        /*target_id=*/99,
        /*msg_cnt=*/1,
        /*proto_version=*/1,
        /*msg_flags=*/0x0001
    );

    // Metadata remains fully mutable prior to execution/transmission
    msg.header().setFlags(0xAA00);
    msg.header().setMessageId(MyMsgId::COMMAND_PACKET);
    msg.header().setMessageType(MyMsgType::PERIODIC);
    msg.header().updateTimestamp(); // Synchronize timestamp token to current epoch

    // ============================================================================
    // 3. Dynamic Key-Value Injection
    // ============================================================================
    // WARNING: .add() is an append-only operation that skips uniqueness validation
    // for absolute execution speed in Release builds. Duplicate keys will leak space
    // on the wire, and .find() will only resolve to the first match.
    // Use .set() if insert-or-overwrite (upsert) semantics are required.
    msg.add("sensor_alpha", "voltage",     msgframe::VALUE(12.6));
    msg.add("sensor_alpha", "status_ok",   msgframe::VALUE(true));
    msg.add("device_core",  "fw_version",  msgframe::VALUE("v3.2.1"));
    msg.add("device_core",  "error_codes", msgframe::VALUE(-5));

    // ============================================================================
    // 4. Raw Zero-Copy Binary Attachments
    // ============================================================================
    // Heavy binary payloads completely bypass the structured parameter map.
    // They are appended to the wire-end to protect the CPU's memory bus.
    std::vector<uint8_t> raw_iq_data = { 0x01, 0x02, 0x03, 0x04, 0x05, 0xAA, 0xBB, 0xCC };
    msg.add_attachment("raw_iq_stream", std::move(raw_iq_data));

    std::cout << "Header Timestamp:  " << msg.header().getTimestamp() << " ms\n";
    std::cout << "Header MsgID:      " << msg.header().getMessageIdRaw() << "\n";
    std::cout << "Header Version:    " << msg.header().getVersion() << "\n";
    std::cout << "Header Flags:      0x" << std::hex << msg.header().getFlags() << std::dec << "\n";
    std::cout << "Total parameters:  " << msg.parameters_size() << "\n";
    std::cout << "Total attachments: " << msg.get_attachments().size() << "\n\n";

    // ============================================================================
    // 5. Lookups, Extraction, and Interrogation
    // ============================================================================
    if (const auto* val = msg.find("sensor_alpha", "voltage")) {
        if (auto current_v = val->tryGetDouble()) {
            std::cout << "Found sensor_alpha.voltage: " << *current_v << " V\n";
        }
    }

    // Low-overhead element iteration via functional callback routing
    msg.iterate_parameters(printParam, nullptr);

    // ============================================================================
    // 6. Serialization & Wire Reconstruction
    // ============================================================================
    std::vector<uint8_t> send_buffer;
    msg.serialize(send_buffer); // Flatten frame for network socket or DMA transfer

    // Target receiver boundary execution
    msgframe::MessageFrame received;
    if (received.deserialize(send_buffer.data(), send_buffer.size())) {
        if (received.header().getMessageType<MyMsgType>() == MyMsgType::PERIODIC) {
            std::cout << "\n[Receiver] Decoded routing frame classification: PERIODIC\n";
        }
        if (const auto* val = received.find("device_core", "fw_version")) {
            if (auto fw = val->tryGetString()) {
                std::cout << "[Receiver] Active firmware verified: " << *fw << "\n";
            }
        }
    }

    return 0;
}
```

## 🏎️ `add()` vs `set()` vs `update()`

The parameter insertion interface is divided into three distinct execution paths. Picking the right variant based on your loop configuration prevents unnecessary runtime overhead and hidden heap actions.

| Execution Metric | `add()` / `add_flat()` | `set()` / `set_flat()` | `update()` / `update_flat()` |
| :--- | :--- | :--- | :--- |
| **Operational Semantic** | Blind Append | Upsert (Insert or Overwrite) | Strict In-place Overwrite Only |
| **Algorithmic Complexity** | $O(1)$ Constant Time | $O(N)$ Vector / $O(1)$ Hash Map | $O(N)$ Vector / $O(1)$ Hash Map |
| **Behavior on Missing Key** | Inserts new parameter | Inserts new parameter | Returns `false`; ignores operation |
| **Behavior on Existing Key** | Appends duplicate (`assert` in Debug) | Modifies value safely in place | Modifies value safely in place |

### 🛑 `add()` / `add_flat()` — Append-Only (No Uniqueness Checks)
* **Vector Mode:** Translates to a direct, raw `push_back()` onto the contiguous block.
* **Map Mode:** Maps to a direct, unconditional bucket `emplace()`.
* **Best Practice:** Use this for fast streaming loops where frames are constructed from scratch deterministically and keys are guaranteed to be unique.
* **Warning:** In Release builds, duplication validation is completely bypassed for absolute performance. If a duplicate is inserted, the packed frame size inflates unnecessarily, and `.find()` will lock onto the *first* instance, masking downstream mutations. Debug builds catch this via an internal `#ifndef NDEBUG assert()`.

### 🔄 `set()` / `set_flat()` — Upsert (Insert or Overwrite)
* Scans the structural tree first. If the key exists, it mutates the value in place; if missing, it registers a fresh parameter entry.
* **Best Practice:** Use this when data streams from disjoint asymmetrical endpoints out of order, or when multiple isolated modules update the same key parameter within the same loop cycle.

### 🎯 `update()` / `update_flat()` — In-Place Edit
* Modifies an entry *only* if it has already been instantiated. It will never grow the container layout.
* **Best Practice:** Perfect for updating shared, static frame templates. Downstream processing blocks can safely update specific fields without being able to inject malicious or unexpected tracking metrics. If the target key is missing, it drops execution and returns `false`.

## ⚡ High-Performance Lookups via Heterogeneous Maps

When `HybridMessageMap` crosses the 128-element barrier and transitions into its hash-map state (`tsl::robin_map`), it activates transparent hashing and equality mechanisms (`ParameterKeyHash` and `ParameterKeyEqual`).

Rather than performing a naive transparent interface built on string pairs (which causes pointer lifetime dependencies and catastrophic cascading routing drops), MessageFrame processes lookups against a unified string footprint.

When executing `msg.find("device_id", "parameter_name")`:
1. The library combines the separate inputs into an internal stack tracking structure.
2. Thanks to **Small String Optimization (SSO)**, the consolidated string lives entirely on the stack frame without triggering heap allocations.
3. The open-addressing table is queried via a raw `std::string_view` anchor, giving cache-resilient $O(1)$ lookup speeds without memory fragmentation or dangling reference drops.

## 🔒 Key Naming & Small String Optimization (SSO)

Because indexing utilizes a unified layout string inside a ParameterKey (modeled as `device` + `internal tracking divider` + `parameter`), short namespace patterns explicitly leverage the compiler's Small String Optimization (SSO). Keeping the combined size under **15 to 23** bytes ensures keys avoid the heap allocator entirely.

> Crucial Structural Rule: **The internal tracking divider is not a dot (.)**. The library
> utilizes the standard ASCII Unit Separator token ('\x1F'). Never construct keys manually
> using custom string formatting (like device + "." + param); always route composition
> through FlatKey::compose(device, param).

### Optimized Micro-Routing with the _flat Suffix

`add_flat()`, `set_flat()`, `update_flat()`, `find_flat()` take a pre-composed `FlatKey` object. It can only be constructed explicitly:

```cpp
auto key = msgframe::FlatKey::compose("sdr1", "frequency"); // Automatically inserts '\x1F'
```

This structural separation handles scenarios where the exact same key coordinates are requested across high-rate looping cycles. Composing it once outside your hot processing code completely bypasses the minor stack-buffer re-assembly step required by the standard two-string path:

```cpp
// find_flat() operates roughly 63% faster per call compared to the two-string lookup path
// in map mode because the key concatenation phase is bypassed completely.
auto freq_key = msgframe::FlatKey::compose("sdr_1", "frequency");
while (processing) {
    // Zero stack-formatting overhead on every single pass
    msg.set_flat(freq_key, msgframe::ParameterValue(read_frequency()));
}
```

### Preferred Object-Oriented Architecture Pattern

For production systems, initialize FlatKey structures inside your component constructors, storing them as immutable fields for the lifecycle of your system drivers:

```cpp
    class TelemetryStreamer {
        private:
            std::string       device_name_;
            msgframe::FlatKey voltage_key_;
            msgframe::FlatKey firmware_key_;
        public:
            explicit TelemetryStreamer(std::string_view name):
                device_name_(name),
                // Evaluated ONCE at startup
                voltage_key_(msgframe::FlatKey::compose(name, "voltage")),
                firmware_key_(msgframe::FlatKey::compose(name, "fw_version"))
                {}

                void execute_loop_pass(msgframe::MessageFrame& frame, double volts, const char* fw) {
                    // Zero allocation, maximum cache line efficiency
                    frame.set_flat(voltage_key_,  msgframe::VALUE(volts));
                    frame.set_flat(firmware_key_, msgframe::VALUE(fw));
                }
    };
```

## ♻️ Operational Recycling via clear()

The `clear()` interface safely prepares a `MessageFrame` instance for high-frequency reuse across sequential processing cycles, eliminating the overhead of continually instantiating and tearing down top-level objects.

```text
[ Default Loop Execution ] -> clear() flushes sizes, drops to vector, keeps internal vector capacities.
[ Sized FrameConfig Loop ] -> clear() flushes elements, preserves active pre-sized tsl::robin_map allocations.
```

## Internal Allocator Lifecycle Rules

- Calling `clear()` preserves the underlying container capacity configurations to achieve steady-state memory behavior over long operational cycles.
- Without a Configuration Hint: `clear()` resets trackers to an empty std::vector layout while keeping its reserved buffer space. If the frame previously grew and migrated to map mode, the map allocation is cleared, and the frame restarts in vector mode.
- With a FrameConfig::initial_reserve Hint: `clear()` flushes tracking counters but **retains the fully allocated tsl::robin_map heap layout**. It bypasses the vector fallback stage entirely, meaning subsequent insertions stay allocation-free and skip layout migration costs.

Proper Processing Loop Pattern:

```cpp
#include <messageframe/MessageFrame.hpp>
#include <vector>

int main() {

    // Setup tuning guidelines for massive frames
    msgframe::FrameConfig cfg;
    cfg.initial_reserve = 1024;

    msgframe::MessageFrame msg(
        /*msg_id=*/1001,
        /*msg_type=*/1,
        /*src_id=*/50,
        /*tgt_id=*/99,
        /*msg_cnt=*/1,
        /*proto_version=*/1,
        /*msg_flags=*/0x0A0A,
        cfg);

    std::vector<uint8_t> buffer;

    while (running) {
        // Fill the message with parameters
        msg.add("sensor_alpha", "voltage", msgframe::VALUE(12.6));
        msg.add("device_core", "fw_version", msgframe::VALUE("v3.2.1"));

        // Serialize and send
        buffer.clear();
        msg.serialize(buffer);
        send(buffer);

        // Crucial: Flushes items but locks the 1024-slot robin_map layout in memory
        msg.clear();
    }
}
```
