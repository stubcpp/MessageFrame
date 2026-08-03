// Project: MessageFrame Library
// File: examples/extended_usage.cpp
// Author: Serjio
// Copyright (c) 2026 Serjio
// SPDX-License-Identifier: MIT
//
// Description:
// Extended usage example. basic_usage.cpp covers the happy path in one
// pass; this file focuses on nuances that a first read won't cover:
// add()/set()/update() semantics, the _flat fast path, value type
// handling, the vector->map transition, FrameConfig sizing hints, safe
// reuse via clear(), and error handling on malformed input.
//
// License:
// This file is part of the MessageFrame library.
// See the LICENSE file in the project root for full license information.
// ============================================================================

#include <messageframe/MessageFrame.hpp>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <string_view>

// Reused across the file for iterate_parameters() calls.
void printParam(std::string_view flat_key, const msgframe::ParameterValue& val, void* /*user_data*/) {
    size_t sep_pos = flat_key.find('\x1F');
    std::cout << " ";
    if (sep_pos != std::string_view::npos) {
        std::cout << flat_key.substr(0, sep_pos) << "." << flat_key.substr(sep_pos + 1);
    } else {
        std::cout << flat_key;
    }
    std::cout << " = " << val.toString() << "\n";
}

// ----------------------------------------------------------------
// 1. add() vs set() vs update() — three different contracts on the
// same two-part key. Picking the wrong one either wastes cycles
// or silently corrupts data.
// ----------------------------------------------------------------
void section_add_set_update() {
    std::cout << "\n=== 1. add() vs set() vs update() ===\n";

    msgframe::MessageFrame msg(1, 1, 0, 0);

    // add(): fastest path, O(1) in vector mode, but it does NOT check for
    // duplicates. Calling add() twice with the same (device, param) pair
    // creates a genuine duplicate entry in vector mode — find() will then
    // return the FIRST one (silently wrong in Release, an assert in Debug).
    // Rule of thumb: only use add() when you know the key is new — first
    // fill of a message, or keys generated from a loop counter.
    msg.add("sensor_alpha", "voltage", msgframe::VALUE(12.0));
    std::cout << "After add(): voltage = "
              << msg.find("sensor_alpha", "voltage")->toString() << "\n";

    // set(): upsert. Updates in place if the key exists, otherwise adds it.
    // Safe to call repeatedly with the same key — size stays the same.
    msg.set("sensor_alpha", "voltage", msgframe::VALUE(12.6));
    std::cout << "After set() on existing key: voltage = "
              << msg.find("sensor_alpha", "voltage")->toString()
              << ", size = " << msg.parameters_size() << "\n";

    msg.set("sensor_alpha", "current", msgframe::VALUE(0.42)); // key didn't exist -> added
    std::cout << "After set() on new key: size = " << msg.parameters_size() << "\n";

    // update(): the strict sibling of set() — modifies ONLY if the key
    // already exists, never grows the container. Returns a bool so you can
    // tell "updated" from "no such key" without a separate find() call.
    bool updated_existing = msg.update("sensor_alpha", "voltage", msgframe::VALUE(12.7));
    bool updated_missing = msg.update("sensor_alpha", "does_not_exist", msgframe::VALUE(0));
    std::cout << "update() on existing key returned: " << std::boolalpha << updated_existing << "\n";
    std::cout << "update() on missing key returned: " << updated_missing
              << " (size unchanged: " << msg.parameters_size() << ")\n";
}

// ----------------------------------------------------------------
// 2. FlatKey and the _flat fast path — skip repeated device+param
// concatenation when the same key is touched many times.
// ----------------------------------------------------------------
void section_flat_key() {
    std::cout << "\n=== 2. FlatKey / _flat fast path ===\n";

    msgframe::MessageFrame msg(1, 1, 0, 0);

    // Compose ONCE, outside any hot loop — this is where the "device.param"
    // concatenation actually happens.
    const auto freq_key = msgframe::FlatKey::compose("sdr_1", "center_freq");

    msg.add_flat(freq_key, msgframe::VALUE(433'000'000.0));

    // Simulate a polling loop that repeatedly updates the same parameter:
    // reuse the same FlatKey instead of passing ("sdr_1", "center_freq")
    // as two strings on every iteration.
    for (int i = 0; i < 3; ++i) {
        msg.set_flat(freq_key, msgframe::VALUE(433'000'000.0 + i * 1000.0));
    }
    std::cout << "center_freq after polling loop: "
              << msg.find_flat(freq_key)->toString() << "\n";

    // For one-off, non-repeated keys the regular multi-key API is simpler
    // and the difference is negligible — don't bother composing a FlatKey
    // for a key you touch once.
}

// ----------------------------------------------------------------
// 3. VALUE() type deduction.
// ----------------------------------------------------------------
void section_value_types() {
    std::cout << "\n=== 3. VALUE() type deduction ===\n";

    msgframe::MessageFrame msg(1, 1, 0, 0);

    msg.add("dev", "int_param", msgframe::VALUE(42)); // -> Int64
    msg.add("dev", "int64_param", msgframe::VALUE(int64_t{-100})); // -> Int64
    msg.add("dev", "double_param", msgframe::VALUE(3.14159)); // -> Double
    msg.add("dev", "bool_param", msgframe::VALUE(true)); // -> Bool
    msg.add("dev", "cstr_param", msgframe::VALUE("firmware_v3")); // -> String
    msg.add("dev", "std_str_param", msgframe::VALUE(std::string("dynamic string"))); // -> String

    msg.iterate_parameters(printParam, nullptr);
}

// ----------------------------------------------------------------
// 4. tryGet*() — safe accessors, including the "wrong type" case.
// ----------------------------------------------------------------
void section_try_get() {
    std::cout << "\n=== 4. tryGet*() safe accessors ===\n";

    msgframe::MessageFrame msg(1, 1, 0, 0);
    msg.add("dev", "name", msgframe::VALUE("unit_7"));

    const auto* val = msg.find("dev", "name");

    // Asking for the WRONG type returns std::nullopt — it does not throw
    // and does not silently reinterpret the bytes. Always check has_value().
    auto as_int = val->tryGetInt();
    std::cout << "tryGetInt() on a string value has_value() = "
              << std::boolalpha << as_int.has_value() << "\n";

    auto as_string = val->tryGetString();
    std::cout << "tryGetString() on a string value: "
              << (as_string ? *as_string : "<none>") << "\n";

    // toString() always succeeds regardless of the underlying type — use it
    // for logging/debugging when you don't care about the concrete type.
    std::cout << "toString() always works: " << val->toString() << "\n";
}

// ----------------------------------------------------------------
// 5. Copy vs move semantics on ParameterValue construction.
// ----------------------------------------------------------------
void section_move_semantics() {
    std::cout << "\n=== 5. Copy vs move semantics ===\n";

    msgframe::MessageFrame msg(1, 1, 0, 0);

    std::string original("this string will be moved");
    auto moved_value = msgframe::VALUE(std::move(original));
    msg.add("dev", "moved_param", std::move(moved_value));

    // After the move, the original ParameterValue resets to Unknown —
    // don't keep using it as if it still holds the string.
    std::cout << "moved-from ParameterValue still has string? "
              << std::boolalpha << moved_value.tryGetString().has_value() << "\n";

    // Passing an lvalue const& instead copies — useful when you still need
    // the source value afterward.
    auto kept_value = msgframe::VALUE(std::string("kept alive by caller"));
    msg.add("dev", "copied_param", kept_value); // copy, kept_value still valid
    std::cout << "copied ParameterValue still has string? "
              << kept_value.tryGetString().has_value() << "\n";
}

// ----------------------------------------------------------------
// 6. The vector -> map transition at SMALL_CAPACITY — transparent
// to the caller, but worth seeing happen at least once.
// ----------------------------------------------------------------
void section_vector_to_map_transition() {
    std::cout << "\n=== 6. Vector -> map transition at SMALL_CAPACITY ===\n";

    msgframe::MessageFrame msg(1, 1, 0, 0);

    const size_t count = msgframe::HybridMessageMap::SMALL_CAPACITY + 10;
    for (size_t i = 0; i < count; ++i) {
        msg.add("dev", "param_" + std::to_string(i), msgframe::VALUE(static_cast<int64_t>(i)));
    }

    std::cout << "Inserted " << msg.parameters_size() << " parameters "
              << "(SMALL_CAPACITY = " << msgframe::HybridMessageMap::SMALL_CAPACITY << ").\n";
    std::cout << "Container has switched to map mode internally — "
              << "find()/add()/set() use exactly the same calls as before:\n";

    const auto* last = msg.find("dev", "param_" + std::to_string(count - 1));
    std::cout << " last inserted param still found: " << std::boolalpha << (last != nullptr) << "\n";
}

// ----------------------------------------------------------------
// 7. FrameConfig — sizing hint for messages known in advance to hold
// many more than SMALL_CAPACITY parameters. Pure opt-in: the
// default (initial_reserve = 0) reproduces section 6's behavior
// exactly.
// ----------------------------------------------------------------
void section_frame_config() {
    std::cout << "\n=== 7. FrameConfig sizing hint ===\n";

    msgframe::FrameConfig config;
    config.initial_reserve = 1024; // expected parameter count, known ahead of time

    msgframe::MessageFrame msg(
        /*msg_id=*/1001, /*msg_type=*/1, /*src_id=*/50, /*tgt_id=*/99,
        /*msg_cnt=*/1, /*proto_version=*/1, /*msg_flags=*/0, config);

    // No vector fill, no vector->map migration, no under-sized reserve():
    // the map is created up front and sized for 1024 entries.
    for (int i = 0; i < 1024; ++i) {
        msg.add("bench", "param_" + std::to_string(i), msgframe::VALUE(i));
    }
    std::cout << "Filled " << msg.parameters_size() << " parameters using an initial_reserve hint.\n";

    // The hint survives clear() — reused MessageFrames in a hot loop don't
    // fall back to vector mode and pay the conversion cost again on every
    // refill. Demonstrated in section 8 below with a smaller fill count on
    // purpose: even far below SMALL_CAPACITY, this instance stays in map
    // mode because the hint was set at construction time.
    msg.clear();
    for (int i = 0; i < 5; ++i) {
        msg.add("bench", "param_" + std::to_string(i), msgframe::VALUE(i));
    }
    std::cout << "After clear() + small refill, size = " << msg.parameters_size()
              << " (still primed for the original hint, not reset to vector mode).\n";
}

// ----------------------------------------------------------------
// 8. clear() + reuse — the canonical pattern for a MessageFrame that
// lives outside a send loop instead of being recreated every time.
// ----------------------------------------------------------------
void section_clear_and_reuse() {
    std::cout << "\n=== 8. clear() + reuse in a loop ===\n";

    msgframe::MessageFrame msg(1001, 1, 50, 99, /*msg_cnt=*/0);
    std::vector<uint8_t> buffer;

    for (int cycle = 0; cycle < 3; ++cycle) {
        msg.header().setMessageCounter(static_cast<uint64_t>(cycle));

        msg.add("sensor_alpha", "voltage", msgframe::VALUE(12.0 + cycle * 0.1));
        msg.add("device_core", "fw_version", msgframe::VALUE("v3.2.1"));

        buffer.clear();
        msg.serialize(buffer);
        std::cout << "Cycle " << cycle << ": serialized " << buffer.size() << " bytes.\n";

        // REQUIRED before refilling — otherwise add() appends duplicates
        // instead of replacing the previous cycle's values.
        msg.clear();
    }
}

// ----------------------------------------------------------------
// 9. Multiple attachments and lookup by name.
// ----------------------------------------------------------------
void section_attachments() {
    std::cout << "\n=== 9. Multiple attachments ===\n";

    msgframe::MessageFrame msg(1, 1, 0, 0);

    msg.add_attachment("raw_iq_stream", { 0x01, 0x02, 0x03, 0x04 });
    msg.add_attachment("spectrum_snapshot", { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE });

    std::cout << "Total attachments: " << msg.get_attachments().size() << "\n";
    for (const auto& att : msg.get_attachments()) {
        std::cout << " " << att.name << ": " << att.raw_data.size() << " bytes\n";
    }

    // No find_attachment() helper exists on purpose — attachments bypass
    // the parameter map entirely, so lookup is a plain linear scan.
    auto it = std::find_if(msg.get_attachments().begin(), msg.get_attachments().end(),
                            [](const msgframe::Attachment& a) { return a.name == "spectrum_snapshot"; });
    std::cout << "spectrum_snapshot found: " << std::boolalpha
              << (it != msg.get_attachments().end()) << "\n";
}

// ----------------------------------------------------------------
// 10. Header: custom routing enums, mutation after construction,
// raw vs typed getters.
// ----------------------------------------------------------------
enum class DeviceClass : int32_t { SDR = 1, SENSOR_HUB = 2 };

void section_header_details() {
    std::cout << "\n=== 10. Header details ===\n";

    // Only msg_id, msg_type, src_id, tgt_id are mandatory. The rest default:
    // msg_cnt=0, proto_version=1, msg_flags=0, config=FrameConfig{}. See
    // basic_usage.cpp for the fully-spelled-out 7-argument form.
    msgframe::MessageFrame msg(DeviceClass::SDR, /*msg_type=*/7, /*src=*/1, /*tgt=*/2);

    std::cout << "At construction, getMessageId<DeviceClass>() = "
              << static_cast<int32_t>(msg.header().getMessageId<DeviceClass>()) << "\n";

    // header() returns a mutable reference — routing metadata isn't fixed
    // at construction. Useful when the final id/type/flags/target are only
    // known partway through building the message, or when the timestamp
    // needs refreshing right before transmission.
    msg.header().setMessageId(DeviceClass::SENSOR_HUB);
    msg.header().setFlags(0xAA00);
    msg.header().setTargetID(42);
    msg.header().updateTimestamp(); // refresh to "now" right before send

    std::cout << "After mutation, getMessageId<DeviceClass>() = "
              << static_cast<int32_t>(msg.header().getMessageId<DeviceClass>()) << "\n";
    std::cout << "After mutation, getTargetID() = " << msg.header().getTargetID() << "\n";
    std::cout << "After mutation, getFlags() = 0x"
              << std::hex << msg.header().getFlags() << std::dec << "\n";

    // Raw getter: use when routing code doesn't know the enum type at all
    // (e.g. a generic dispatcher that only forwards by numeric ID).
    std::cout << "getMessageIdRaw() = " << msg.header().getMessageIdRaw() << "\n";
}

// ----------------------------------------------------------------
// 11. Error handling: deserialize() on malformed input must not throw
// or crash — it returns false and leaves the object usable.
// ----------------------------------------------------------------
void section_error_handling() {
    std::cout << "\n=== 11. Error handling on malformed input ===\n";

    std::vector<uint8_t> garbage = { 0xFF, 0x00, 0x13, 0x37, 0xDE, 0xAD };

    msgframe::MessageFrame msg;
    bool ok = msg.deserialize(garbage.data(), garbage.size());
    std::cout << "deserialize() on garbage bytes returned: " << std::boolalpha << ok << "\n";

    bool ok_empty = msg.deserialize(nullptr, 0);
    std::cout << "deserialize() on null/empty input returned: " << ok_empty << "\n";
}

int main() {
    section_add_set_update();
    section_flat_key();
    section_value_types();
    section_try_get();
    section_move_semantics();
    section_vector_to_map_transition();
    section_frame_config();
    section_clear_and_reuse();
    section_attachments();
    section_header_details();
    section_error_handling();
    return 0;
}