// ============================================================================
// Project: MessageFrame Library
// File:    Value.hpp
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
#include <optional>
#include <cstdint>


namespace msgframe {

    class ParameterValue {
    public:
        enum class Type : uint8_t {
            Unknown = 0,
            Int64 = 10,
            Double = 20,
            String = 30,
            Bool = 40
        };
    
        ParameterValue() noexcept;
        explicit ParameterValue(int64_t v) noexcept;
        explicit ParameterValue(double v) noexcept;
        explicit ParameterValue(const std::string& v);
        explicit ParameterValue(std::string&& v) noexcept;
        explicit ParameterValue(const char* v);
        explicit ParameterValue(bool v) noexcept;

        ParameterValue(const ParameterValue& other);
        ParameterValue(ParameterValue&& other) noexcept;
        ParameterValue& operator=(const ParameterValue& other);
        ParameterValue& operator=(ParameterValue&& other) noexcept;
        ~ParameterValue();

        // Setters
        void setValue(int64_t v) noexcept;
        void setValue(double v) noexcept;
        void setValue(const std::string& v);
        void setValue(std::string&& v) noexcept;
        void setValue(bool v) noexcept;

        Type getType() const noexcept { return type; }

		// methods for MessagePack serialization
        void pack(void* packer_ptr) const;
        void unpack(const void* object_ptr);

		// Getters
        std::optional<int64_t> tryGetInt() const noexcept;
        std::optional<double> tryGetDouble() const noexcept;
        std::optional<std::string> tryGetString() const;
        std::optional<bool> tryGetBool() const noexcept;

        std::string toString() const;

    private:
        void reset() noexcept;
		
		// Helper methods for safely accessing a string inside a buffer
        std::string& as_string() noexcept {
            return *reinterpret_cast<std::string*>(value.stringBuffer);
        }
        const std::string& as_string() const noexcept {
            return *reinterpret_cast<const std::string*>(value.stringBuffer);
        }

        Type type{Type::Unknown};
        
        // Using a raw buffer aligned to the size of std::string.
        union ValueUnion {
            int64_t intValue;
            double doubleValue;
            bool boolValue;
            alignas(std::string) char stringBuffer[sizeof(std::string)];

            ValueUnion() noexcept { intValue = 0; }
            ~ValueUnion() noexcept {}
        } value;
    };

    // Fabric function
    template <typename T>
    ParameterValue VALUE(T&& val) {
        using DecayedT = std::decay_t<T>;
        
        if constexpr (std::is_same_v<DecayedT, bool>) {
            return ParameterValue(static_cast<bool>(val));
        } else if constexpr (std::is_integral_v<DecayedT>) {
            return ParameterValue(static_cast<int64_t>(val));
        } else if constexpr (std::is_floating_point_v<DecayedT>) {
            return ParameterValue(static_cast<double>(val));
        } else if constexpr (std::is_same_v<DecayedT, std::string>) {
            return ParameterValue(std::forward<T>(val));
        } else if constexpr (std::is_convertible_v<DecayedT, const char*>) {
            return ParameterValue(static_cast<const char*>(val));
        } else {
            static_assert(std::is_void_v<DecayedT>, "Unsupported type for msgframe::VALUE");
            return ParameterValue();
        }
    }
}
