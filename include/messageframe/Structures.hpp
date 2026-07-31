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
        std::string full_key; // Stored as "device.parameter"

        // Optimized constructor that stitches the key in one pass
        ParameterKey(std::string_view device, std::string_view param) {
            full_key.reserve(device.size() + 1 + param.size());
            full_key.append(device).append(".").append(param);
        }

        // Direct transfer or copy constructor for flat methods
        explicit ParameterKey(std::string key) : full_key(std::move(key)) {}

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

        // Comparing the internal full_key with the pair (device, param)
        bool operator()(const ParameterKey& lhs, std::pair<std::string_view, std::string_view> rhs) const noexcept {
            std::string_view l_view(lhs.full_key);
            if (l_view.size() != rhs.first.size() + 1 + rhs.second.size()) return false;
            if (l_view.compare(0, rhs.first.size(), rhs.first) != 0) return false;
            if (l_view[rhs.first.size()] != '.') return false;
            return l_view.substr(rhs.first.size() + 1) == rhs.second;
        }

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

        // Hash from two string_views (called during find)
        std::size_t operator()(std::string_view flat_key) const noexcept {
            return std::hash<std::string_view>{}(flat_key);
        }

        // Hashes the pair on the fly, simulating a combination key
        std::size_t operator()(std::pair<std::string_view, std::string_view> p) const noexcept {
            std::size_t h1 = std::hash<std::string_view>{}(p.first);
            std::size_t h2 = std::hash<std::string_view>{}(p.second);
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
