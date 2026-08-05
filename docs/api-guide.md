# API Guide

## Full usage example

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
enum class MyMsgId : int32_t {
    TELEMETRY_PACKET = 1001,
    COMMAND_PACKET   = 1002
};

// MyMsgType is a lightweight, orthogonal classification tag — it doesn't say
// *what* the message is, only *how* it should be treated (priority, urgency,
// delivery semantics). The same MsgId can show up with different MsgTypes:
// a TELEMETRY_PACKET might be PERIODIC most of the time, but CRITICAL when a
// sensor crosses a threshold.
enum class MyMsgType : int32_t {
    PERIODIC = 1,
    CRITICAL = 2
};

// A simple callback used to demonstrate fast, allocation-free iteration
void printParam(std::string_view flat_key, const msgframe::ParameterValue& val, void* /*user_data*/) {
    // Find the position of our internal guard separator \x1F
    size_t sep_pos = flat_key.find('\x1F');

    std::cout << "  [Iterate] ";
    if (sep_pos != std::string_view::npos) {
        // Print the part before the separator (device), the period, and the part after (parameter)
        std::cout << flat_key.substr(0, sep_pos) << "." << flat_key.substr(sep_pos + 1);
    } else {
        std::cout << flat_key;
    }
    std::cout << " = " << val.toString() << "\n";
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
        MyMsgId::TELEMETRY_PACKET,
        MyMsgType::CRITICAL,
        /*source_id=*/50,
        /*target_id=*/99,
        /*msg_cnt=*/1,
        /*proto_version=*/1,
        /*msg_flags=*/0x0001);

    // Every field is also reachable after construction — useful when a
    // message is reused or re-purposed before sending.
    msg.header().setFlags(0xAA00);
    msg.header().setMessageId(MyMsgId::COMMAND_PACKET);
    msg.header().setMessageType(MyMsgType::PERIODIC);
    msg.header().updateTimestamp(); // refresh to "now" right before transmission

    // ----------------------------------------------------------------
    // 2. Add parameters using the two-key API (device, parameter, value)
    // ----------------------------------------------------------------

    // WARNING: add() does NOT check if the "sensor_alpha" / "voltage" key
    // combination already exists. In Release builds, it bypasses safety
    // checks for maximum speed and blindly appends duplicates.
    //
    // What happens if you do:
    // 1. The serialized MessagePack frame size grows unnecessarily.
    // 2. msg.find() always returns ONLY the first inserted value,
    //    silently ignoring all subsequent duplicates.
    //
    // If you need to safely insert-or-overwrite existing keys, use set() instead.
    msg.add("sensor_alpha", "voltage",     msgframe::VALUE(12.6));
    msg.add("sensor_alpha", "status_ok",   msgframe::VALUE(true));
    msg.add("device_core",  "fw_version",  msgframe::VALUE("v3.2.1"));
    msg.add("device_core",  "error_codes", msgframe::VALUE(-5));

    // ----------------------------------------------------------------
    // 3. Attach a raw binary payload (e.g. IQ samples, a spectrum snapshot)
    //    Attachments bypass the parameter map entirely.
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
        if (auto current_v = val->tryGetDouble()) {
            std::cout << "Found sensor_alpha.voltage: " << *current_v << " V\n";
        }
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
        if (const auto* val = received.find("device_core", "fw_version")) {
            if (auto fw = val->tryGetString()) {
                std::cout << "Decoded device_core.fw_version: " << *fw << "\n";
            }
        }
    }

    return 0;
}
```

## `add()` vs `set()` vs `update()`

The insertion API is split into three modes, each with a different
cost/safety trade-off. Picking the right one for a given call site keeps
hot paths allocation-free where it matters.

|                        | `add()` / `add_flat()`                 | `set()` / `set_flat()`           | `update()` / `update_flat()`   |
|------------------------|-----------------------------------------|-----------------------------------|-----------------------------------|
| **Semantics**          | Append, no duplicate check             | Upsert (insert or overwrite)     | Strict in-place edit only        |
| **Complexity**         | O(1)                                   | O(N) vector-mode, O(1) map-mode  | O(N) vector-mode, O(1) map-mode  |
| **On missing key**     | Inserts                                | Inserts                          | Returns `false`, no change       |
| **On existing key**    | Duplicate (Release) / `assert` (Debug) | Overwrites                       | Overwrites                       |

### `add()` / `add_flat()` — append-only, no duplicate check

In vector mode this is a plain `push_back()`; in map mode, an `emplace()`.
Use it for high-frequency streams where you assemble a frame from scratch
in a deterministic loop and know each key is unique. `add_flat()` takes a
pre-composed `FlatKey` (see below) instead of separate `device`/`param`
arguments.

Be careful: a duplicate key bypasses the check in Release builds (the
vector-mode path doesn't scan for existing entries, by design, to stay
O(1)) — `find()` will then return whichever entry came first, silently. In
Debug builds (`#ifndef NDEBUG`), an `assert()` catches this during
development.

### `set()` / `set_flat()` — upsert

Looks for the key first; if found, overwrites it in place, otherwise
inserts. Use it when parameters can arrive out of order, or when multiple
subsystems might write to the same device/parameter pair within one frame
cycle. In vector mode this costs an O(N) linear scan before the eventual
insert; in map mode it's a single lookup + assign.

### `update()` / `update_flat()` — strict in-place edit

Modifies an existing entry and never grows the container. Useful for
pre-populated frame templates, where a downstream stage should only be
allowed to adjust fields that already exist — `update()` returns `false`
(and leaves the container untouched) if the key isn't there, instead of
silently creating it.

## Zero-allocation lookups via heterogeneous maps

When `HybridMessageMap` crosses the `SMALL_CAPACITY = 128` boundary and
falls back to its hash-map mode (`tsl::robin_map`), it uses transparent
hash predicates (`ParameterKeyHash` and `ParameterKeyEqual`).

Rather than a naive transparent implementation built on runtime
`std::pair` wrappers — which risks dangling references during cascaded map
routing — MessageFrame resolves queries against a single flat string
layout. Calling `msg.find("device_id", "parameter_name")` internally
concatenates the two keys into a temporary `std::string` buffer. Thanks to
Small String Optimization (SSO), this combined key resides entirely on
the stack with zero heap allocations, and the hash table is then queried
via a `std::string_view`.

## Key naming and Small String Optimization (SSO)

Since internal indexing relies on a consolidated single-string layout
inside a `ParameterKey` (`device` + the library's internal separator +
`param`), short naming patterns trigger Small String Optimization (SSO).
Keeping combined lengths under ~15–23 bytes keeps keys on the stack,
avoiding heap allocation.

> **The internal separator is not a literal dot.** Earlier examples used
> `"device.parameter"` as illustrative shorthand — the real separator is
> the ASCII Unit Separator (`'\x1F'`). Never build a flat key by hand
> (`device + "." + param` or any other string concatenation); always go
> through `FlatKey::compose(device, param)`. Composing it yourself with
> the wrong character silently stores the entry under an empty device
> instead of raising an error.

### Methods with the `_flat` suffix

`add_flat()`, `set_flat()`, `update_flat()`, `find_flat()` take a
`FlatKey` — a small pre-composed key type. It cannot be constructed from a
raw string; the only way to get one is:

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
device/parameter pair (map-mode, past the 128-parameter threshold):
`find_flat()` was ~63% faster per call than re-supplying `device`/`param`
to `find()` each time, because the two-key path still re-appends
`device` + separator + `param` into a stack buffer on every call — cheap
(SSO avoids a heap allocation), but not free at high call rates. If your
key is only used once per message, plain `add()`/`find()` with separate
`device`/`param` is simpler and the difference won't matter.

**Real-world pattern:** store the `FlatKey` as a member of the object that
owns the device — compose it once in the constructor, reuse it for the
lifetime of the object across every hot-loop call:

```cpp
#include <messageframe/MessageFrame.hpp>
#include <string>

class TelemetryStreamer {
private:
    std::string m_name;
    msgframe::FlatKey m_voltage_key;
    msgframe::FlatKey m_firmware_key;

public:
    // Constructor runs ONCE, e.g. at startup.
    explicit TelemetryStreamer(std::string_view name)
        : m_name(name),
          m_voltage_key(msgframe::FlatKey::compose(name, "voltage")),
          m_firmware_key(msgframe::FlatKey::compose(name, "fw_version"))
    {}

    // Runs thousands of times per second in the hot loop.
    void process(msgframe::MessageFrame& frame, double volts, const char* fw) {
        // Zero per-call key-composition overhead — reuses the keys
        // that were already built once in the constructor.
        frame.set_flat(m_voltage_key, msgframe::VALUE(volts));
        frame.set_flat(m_firmware_key, msgframe::VALUE(fw));
    }
};
```

## What `clear()` does

`clear()` releases the container's current storage and re-applies the
original `FrameConfig` hint (the same setup routine the constructor uses).
Its purpose is to let you reuse the same `MessageFrame` for many
consecutive messages without constructing a new object each time — but
you must call it, or `add()` will keep appending to the previous message
instead of starting fresh.

- **Without a hint** (default), `clear()` resets to vector mode. If the
  message exceeded `SMALL_CAPACITY` before, it migrates back to map mode
  the next time it's filled past the threshold — same as before this
  feature existed.
- **With a `FrameConfig::initial_reserve` hint**, `clear()` goes straight
  back into map mode, sized for the hinted count, so the next fill never
  pays for a vector-to-map migration.

Either way, `clear()` frees the previous vector/map allocation and
re-creates it rather than reusing it in place — the benefit of the hint is
that the *next* fill skips the migration step, not that the old
allocation survives across `clear()`. See
[architecture.md](architecture.md#behavior-across-clear) for the
implementation detail.

Correct usage inside a loop:

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
