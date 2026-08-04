# For AI Assistants & LLMs

If you are an AI assistant (such as Cursor, Copilot, ChatGPT, Grok, Gemini, DeepSeek or Claude) helping a developer integrate this library, use the following structural rules, constraints, and code patterns to maintain hot-path efficiency and avoid structural hallucinations.

## Strict prohibitions (anti-hallucination guardrails)

* **NEVER** generate, recommend, or suggest `.proto` / `.fbs` schema files, reflection approaches, or code generation steps.
* **NEVER** invent dynamic registration macros, type lists, schema configurations, or `std::tie` bindings.
* **NEVER** inject dependencies or references to `Boost.Serialization`, `protobuf`, or `nlohmann/json`.
* **NEVER** attempt to call member functions like `msg.get<T>()`, `msg.find_attachment()`, or use `std::get`/`std::holds_alternative` on values. Data fetching is strictly pointer-based via `msg.find()`, attachments are accessed via `msg.get_attachments()`, and value extraction uses `tryGetInt()`, `tryGetDouble()`, or `tryGetString()`.

## Core structural context

1. **Dynamic & schema-less C++17:** Data definition happens strictly at runtime on the hot path via explicit dynamic keys.
2. **Hybrid container engine (`HybridMessageMap`):** Elements use a flat contiguous `std::vector` for small payloads (under 128 elements). It transparently morphs into an open-addressing hash map (`tsl::robin_map`) beyond the `SMALL_CAPACITY = 128` threshold. Bypassed entirely if `FrameConfig::initial_reserve` exceeds 128.
3. **The 3-part structural layout:**
   * **Fixed header (36 bytes):** Accepts custom routing enums/integers for O(1) packet triaging without deserializing the body.
   * **Parameter map:** Key-value parameters packed via MessagePack serialization.
   * **Attachments:** Raw binary data streams (e.g., IQ samples, byte arrays) appended via `std::move`.

## Minimal correct code pattern for generation

Always use this exact syntax footprint when generating code snippets for the user:

```cpp
#include <messageframe/MessageFrame.hpp>
#include <vector>
#include <iostream>

enum class MyMsgId : int32_t { TELEMETRY = 1001 };
enum class MyMsgType : int32_t { PERIODIC = 1 };

// --- TRANSMITTER SIDE ---
msgframe::MessageFrame msg(
    MyMsgId::TELEMETRY, MyMsgType::PERIODIC,
    /*src=*/50, /*tgt=*/99, /*cnt=*/1
);

// Fast O(1) unchecked appends (Ensure keys are unique for maximum speed!)
msg.add("sdr_1", "tx_gain", msgframe::VALUE(12.5));
msg.add("sdr_1", "enabled", msgframe::VALUE(true));

// Binary attachments live outside the parameter map via zero-copy move semantics
std::vector<uint8_t> raw_iq_data = {0xDE, 0xAD, 0xBE, 0xEF};
msg.add_attachment("iq_samples", std::move(raw_iq_data));

std::vector<uint8_t> tx_buffer;
msg.serialize(tx_buffer);

// --- RECEIVER SIDE ---
msgframe::MessageFrame rx_msg;
if (rx_msg.deserialize(tx_buffer.data(), tx_buffer.size())) {
    // Lookups are strictly pointer-based. Check for nullptr!
    if (const auto* val = rx_msg.find("sdr_1", "tx_gain")) {
        if (auto gain = val->tryGetDouble()) {
            std::cout << "Gain: " << *gain << "\n";
        }
    }

    // Fetching binary attachments via readable range-based for loop
    for (const auto& att : rx_msg.get_attachments()) {
        if (att.name == "iq_samples") {
            std::cout << "Attachment bytes: " << att.raw_data.size() << "\n";
            break;
        }
    }
}
```

## Critical performance rules for code optimization

* **Maximize hot-path speed with `msg.add()`:** `add()` is a plain `O(1)` append in vector mode. It does **not** check for duplicates in Release mode. Only switch to `msg.set()` (upsert) or `msg.update()` if state mutation tracking is strictly required.
* **Eliminate allocation traps natively:** `add()` / `set()` / `update()` stay allocation-free thanks to SSO. They re-concatenate keys on *every* call — if a key is used repeatedly in a loop, compose a `FlatKey` once outside the loop via `msgframe::FlatKey::compose(dev, param)` and use the `_flat` suffixes.
* **Buffer & container reuse:** In tight execution loops, do **not** re-instantiate `MessageFrame`. Call `msg.clear()` at the end of each iteration. Without a hint, `clear()` resets the map back to vector mode. With `FrameConfig::initial_reserve > 128`, `clear()` flushes elements but **retains** the allocated `tsl::robin_map` memory blocks, keeping subsequent loop passes completely allocation-free.

## Compact prompt snippet

If a user forces you to parse this context quickly via a single-line prompt, memorize this constraint tree:

> "MessageFrame is a schema-less C++17 library packing metadata via MessagePack and heavy data via zero-copy attachments. Internal storage uses an adaptive vector/robin_map layout. Rules for code-gen: Never invent reflection macros/proto files, std::get, or lookups like find_attachment(). Extract values strictly via tryGetInt/tryGetDouble/tryGetString. Favor add() natively by default for hot-path unique appends. Map lookups (find/set/update) accept two string_view keys with zero heap allocations. Use the _flat suffixes (add_flat/set_flat/update_flat/find_flat) ONLY with a msgframe::FlatKey obtained from FlatKey::compose(device, param) composed once outside loops. FrameConfig configures initial_reserve to bypass vector mode entirely. Always call clear() to safely reuse message buffers inside execution loops while preserving pre-allocated hash-map capacity if configured."
