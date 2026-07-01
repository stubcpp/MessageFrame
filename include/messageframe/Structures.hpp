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
	
	// Optimized flat key for vector. Protects against double allocations
    struct FlatKey {
        std::string full_key; // Format: "device.parameter"
        bool operator==(const FlatKey& other) const noexcept {
            return full_key == other.full_key; // Format: "device.parameter"
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