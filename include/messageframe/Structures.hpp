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
}
