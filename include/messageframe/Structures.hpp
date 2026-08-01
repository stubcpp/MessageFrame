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

        // Direct transfer or copy constructor for flat methods
        explicit ParameterKey(std::string key) : full_key(std::move(key)) {}
        explicit ParameterKey(std::string_view key) : full_key(key) {}

        // Optimized constructor that stitches the key in one pass
        ParameterKey(std::string_view device, std::string_view param) {
            full_key.reserve(device.size() + 1 + param.size());
            full_key.append(device).append(".").append(param);
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

        // Comparing the internal full_key with the pair (device, param)
        bool operator()(const ParameterKey& lhs, std::pair<std::string_view, std::string_view> rhs) const noexcept {
            std::string_view l_view(lhs.full_key);
            if (l_view.size() != rhs.first.size() + 1 + rhs.second.size()) return false;

            // Direct step-by-step comparison without unnecessary creation of substring objects
            return l_view.compare(0) &&
                l_view[rhs.first.size()] == '.' &&
                l_view.compare(rhs.first.size() + 1, rhs.second.size(), rhs.second) == 0;

        }

        bool operator()(const ParameterKey& lhs, std::string_view rhs_flat) const noexcept {
            return lhs.full_key == rhs_flat;
        }
    };

    // A hasher for tsl::robin_map that hashes both strings together without concatenating them
    struct ParameterKeyHash {
        using is_transparent = void; // Allows hashing string_view on the fly

        // Covers device+"."+param comfortably for realistic key names (README recommends
        // keeping them short for SSO anyway); pathologically long keys fall back to the heap.
        static constexpr std::size_t kStackBufSize = 256;

        // Hash from the finished key
        std::size_t operator()(const ParameterKey& k) const noexcept {
            return std::hash<std::string>{}(k.full_key);
        }

        // Hash from an already-flattened "device.param" string_view (called during find)
        std::size_t operator()(std::string_view flat_key) const noexcept {
            return std::hash<std::string_view>{}(flat_key);
        }

        // Hashes (device, param) as if it were the concatenated "device.param" string,
        // by actually assembling those exact bytes in a stack buffer (no heap allocation
        // for the common case) and reusing std::hash<string_view> -- keeps this bit-for-bit
        // consistent with the two overloads above.
        std::size_t operator()(std::pair<std::string_view, std::string_view> p) const noexcept {
            const std::size_t total = p.first.size() + 1 + p.second.size();
            if (total <= kStackBufSize) {
                char buf[kStackBufSize]{ 0 };
                std::memcpy(buf, p.first.data(), p.first.size());
                buf[p.first.size()] = '.';
                std::memcpy(buf + p.first.size() + 1, p.second.data(), p.second.size());
                return std::hash<std::string_view>{}(std::string_view(buf, total));
            }
            // Rare fallback for keys longer than kStackBufSize.
            std::string tmp;
            tmp.reserve(total);
            tmp.append(p.first).append(1, '.').append(p.second);
            return std::hash<std::string>{}(tmp);
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

    // is_transparent — a tag that tells tsl::robin_map: "this functor can
    // accept multiple compatible key types, not just Key". A custom hash is needed,
    // because std::hash<std::strng> does not accept std::string_view (no conversion,
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
