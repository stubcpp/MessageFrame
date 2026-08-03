// ============================================================================
// Project: MessageFrame Library
// File:    Structures.hpp
// Author:  Serjio
// Copyright (c) 2026 Serjio
// SPDX-License-Identifier: MIT
//
// Description:
//   Shared types (FlatKey, Attachment, VectorBuffer)
//
// License:
//   This file is part of the MessageFrame library.
//   See the LICENSE file in the project root for full license information.
// ============================================================================
#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>


namespace msgframe {
	
    // Optimized key-container. Protects against allocations for string concatenation
    struct ParameterKey {
        std::string full_key; // Stored as "device.parameter"

        // Direct transfer or copy constructor for flat methods
        explicit ParameterKey(std::string key) : full_key(std::move(key)) {}
        explicit ParameterKey(std::string_view key) : full_key(key) {}

        // Optimized constructor that stitches the key in one pass
        ParameterKey(std::string_view device, std::string_view param) {
            full_key.reserve(device.size() + 1 + param.size());
            full_key.append(device).append("\x1F").append(param); // Using "\x1F" as separator
        }

        bool operator==(const ParameterKey& other) const noexcept {
            return full_key == other.full_key;
        }
    };

    struct ParameterKeyEqual {
        using is_transparent = void; // Enables the magic of heterogeneous search

        // Key to Key comparison (for internal map needs)
        bool operator()(const ParameterKey& lhs, const ParameterKey& rhs) const noexcept {
            return lhs.full_key == rhs.full_key;
        }

        // Instead of std::pair now only one flat string_view
        bool operator()(const ParameterKey& lhs, std::string_view rhs_flat) const noexcept {
            return lhs.full_key == rhs_flat;
        }
    };

    // A hasher for tsl::robin_map that hashes both strings together without concatenating them
    struct ParameterKeyHash {
        using is_transparent = void; // Allows hashing string_view on the fly

        // Hash from the finished key
        std::size_t operator()(const ParameterKey& k) const noexcept {
            return std::hash<std::string>{}(k.full_key);
        }

        // Hash from an already-flattened "device.param" string_view (called during find)
        std::size_t operator()(std::string_view flat_key) const noexcept {
            return std::hash<std::string_view>{}(flat_key);
        }
    };

    // A pre-composed, type-safe key for the *_flat() fast-path methods
    // (add_flat / set_flat / update_flat / find_flat).
    //
    // The ONLY way to obtain a FlatKey is FlatKey::compose(device, param),
    // which inserts the library's real separator ('\x1F', ASCII Unit
    // Separator — NOT a literal dot, despite what "device.parameter" in
    // older docs/examples might suggest). This makes it impossible to
    // accidentally pass a raw string with the wrong separator (e.g. a
    // literal ".") or no separator at all — both previously caused
    // add_flat() to silently store the entry under an empty device,
    // with no error and no way to find it again via the original string.
    //
    // Intended use: compose() once outside a hot loop, then reuse the
    // same FlatKey across many repeated add_flat()/find_flat() calls on
    // the same device+parameter pair (e.g. polling "sdr1"+"frequency"
    // every sample) to skip the per-call key concatenation.
    class FlatKey {
    public:
        static FlatKey compose(std::string_view device, std::string_view param) {
            std::string key;
            key.reserve(device.size() + 1 + param.size());
            key.append(device).append(1, kSeparator).append(param);
            return FlatKey(std::move(key));
        }

        std::string_view view() const noexcept { return key_; }

    private:
        explicit FlatKey(std::string key) : key_(std::move(key)) {}
        std::string key_;

        static constexpr char kSeparator = '\x1F';
    };

	// Large raw binary data (IQ-ether, files)
	struct Attachment {
		std::string name;
		std::vector<uint8_t> raw_data;
	};

    // Special adapter that allows msgpack::packer to write DIRECTLY to std::vector
    // This completely removes the intermediate sbuffer and eliminates unnecessary malloc/memcpy!
    class VectorBuffer {
    public:
        VectorBuffer(std::vector<uint8_t>& buffer) : m_buf(buffer) {
            m_buf.clear();
            if (m_buf.capacity() < 1024) {
                m_buf.reserve(1024);
            }
        }

        void write(const char* buf, size_t len) {
            const auto* u_buf = reinterpret_cast<const uint8_t*>(buf);
            m_buf.insert(m_buf.end(), u_buf, u_buf + len);
        } 

    private:
        std::vector<uint8_t>& m_buf;
    };

    // Optional construction/reset hint for HybridMessageMap / MessageFrame.
    // Lets the caller tell the library the expected final parameter count so it
    // can pick storage mode up front instead of filling a vector to
    // SMALL_CAPACITY and then migrating it into the map with an
    // under-sized reserve().
    //
    // NOTE: this does NOT make SMALL_CAPACITY itself configurable — see
    // HybridMessageMap for why (unpack() has no access to a receiver-side
    // config, and the threshold is used as a compile-time constant in
    // several hot paths). It only controls the initial mode/capacity.
    struct FrameConfig {
        size_t initial_reserve = 0; // 0 = unknown, today's behavior unchanged
    };
}
