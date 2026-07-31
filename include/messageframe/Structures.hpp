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
#include <cstring>


namespace msgframe {
	
    // Optimized key-container. Protects against allocations for string concatenation
    struct ParameterKey {
        std::string device;
        std::string parameter;

        // Equal operator for std::find_if та tsl::robin_map
        bool operator==(const ParameterKey& other) const noexcept {
            return device == other.device && parameter == other.parameter;
        }
    };

    struct ParameterKeyEqual {
        using is_transparent = void; // Enables the magic of heterogeneous search

        // Key to Key comparison (for internal map needs)
        bool operator()(const ParameterKey& lhs, const ParameterKey& rhs) const noexcept {
            return lhs.device == rhs.device && lhs.parameter == rhs.parameter;
        }

        // Compare Key with two std::string_view (for fast find)
        // This will allow you to search the map without creating temporary std::strings!
        bool operator()(const ParameterKey& lhs, std::pair<std::string_view, std::string_view> rhs) const noexcept {
            return lhs.device == rhs.first && lhs.parameter == rhs.second;
        }
    };

    // A hasher for tsl::robin_map that hashes both strings together without concatenating them
    struct ParameterKeyHash {
        using is_transparent = void; // Allows hashing string_view on the fly

        // Hash from the finished key
        std::size_t operator()(const ParameterKey& k) const noexcept {
            return hash_combine(k.device, k.parameter);
        }

        // Hash from two string_views (called during find)
        std::size_t operator()(std::pair<std::string_view, std::string_view> p) const noexcept {
            return hash_combine(p.first, p.second);
        }

    private:
        // Helper fast hasher
        std::size_t hash_combine(std::string_view s1, std::string_view s2) const noexcept {
            std::size_t h1 = std::hash<std::string_view>{}(s1);
            std::size_t h2 = std::hash<std::string_view>{}(s2);
            return h1 ^ (h2 << 1);
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
        }
        void write(const char* buf, size_t len) {
            size_t old_size = m_buf.size();
            m_buf.resize(old_size + len);
            std::memcpy(m_buf.data() + old_size, buf, len);
        }
    private:
        std::vector<uint8_t>& m_buf;
    };

    // is_transparent — a tag that tells tsl::robin_map: "this functor can
    // accept multiple compatible key types, not just Key". A custom hash is needed,
    // because std::hash<std::string> does not accept std::string_view (no conversion,
    // this is an intentional limitation of the standard — without it, every hash() could silently
    // allocate a std::string). std::equal_to<> is already transparent out of the box (it has
    // is_transparent and a template operator()).
    struct StringViewHash {
        using is_transparent = void;
        size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }
    };
}
