// ============================================================================
// Project: MessageFrame Library
// File:    HybridMessageMap.hpp
// Author:  Serjio
// Copyright (c) 2026 Serjio
// SPDX-License-Identifier: MIT
//
// Description:
//   Hybrid container (hybrid message map)
//
// License:
//   This file is part of the MessageFrame library.
//   See the LICENSE file in the project root for full license information.
// ============================================================================
#pragma once
#include "messageframe/Structures.hpp"
#include "messageframe/Value.hpp"
#include <memory>
#include <vector>
#include <utility>
#include <string_view>
#include <algorithm>   
#include <cassert> 


namespace msgframe {
    class HybridMessageMap {
    public:
        static constexpr size_t SMALL_CAPACITY = 128;

        HybridMessageMap();
        ~HybridMessageMap();

        // Moving is allowed, copying is prohibited
        HybridMessageMap(const HybridMessageMap&) = delete;
        HybridMessageMap& operator=(const HybridMessageMap&) = delete;
        HybridMessageMap(HybridMessageMap&&) noexcept;
        HybridMessageMap& operator=(HybridMessageMap&&) noexcept;

        // Adding parameters (Zero-Allocation)
        // CRITICAL. Repeated add() with the same key will create a duplicate entry
        // in vector mode (undefined behavior for subsequent find — it will return the first)
        // and will lead to incorrect behavior. Use set() for upsert semantics.
        /**
        * @brief Adds a new parameter without checking for duplicates (Maximum speed).
        * @note Complexity: O(1). In vector mode it performs a pure push_back.
        * @warning If the key already exists, in Release build a duplicate will be created
        *          (find() will return the first). In Debug build an assert will trigger.
        *          Use set() for "insert or update" logic.
        * @param device Name of the device / domain (e.g., "sensor_alpha")
        * @param param  Name of the metric (e.g., "voltage")
        * @param val    Parameter value
        */
        void add(std::string_view device, std::string_view param, const ParameterValue& val); // For lvalue (regular variables) — do const& to avoid copying on input
        void add(std::string_view device, std::string_view param, ParameterValue&& val);      // For rvalue (temporary objects / moved variables) — pure move
        template<typename T>
        void add_impl(std::string_view device, std::string_view param, T&& val) {
            std::string flat_key;
            flat_key.reserve(device.size() + 1 + param.size());
            flat_key.append(device).append(".").append(param);

            // Pass it as const, add_flat will decide when to copy
            add_flat(flat_key, std::forward<T>(val));
        }
		
        // Adding parameters if the key is already combined ("device.parameter")
        /**
        * @brief Adds a new parameter when the key is already combined ("device.parameter")
        *        without checking for duplicates (Maximum speed).
        * @note Complexity: O(1). In vector mode it performs a pure push_back.
        * @warning If the key already exists, in Release build a duplicate will be created
        *          (find() will return the first). In Debug build an assert will trigger.
        *          Use set() for "insert or update" logic.
        * @param flat_key Combined key (e.g., "device.parameter")
        * @param val      Parameter value
        */
		void add_flat(std::string_view flat_key, const ParameterValue& val);
        void add_flat(std::string_view flat_key, ParameterValue&& val);
        template<typename T>
        void add_flat_impl(std::string_view flat_key, T&& val);

        // Upsert: updates the value if the key exists; otherwise adds a new one
        // (slower than add() — linear search in vector mode)
        /**
        * @brief Upsert semantics: Updates the value if the key already exists, or adds a new one.
        * @note Complexity: in vector mode O(N) (linear duplicate search), in map mode O(1).
        * @param device Name of the device / domain
        * @param param  Name of the metric
        * @param val    New value to insert or update
        */
        void set(std::string_view device, std::string_view param, const ParameterValue& val);
        void set(std::string_view device, std::string_view param, ParameterValue&& val);
        template<typename T>
        void set_impl(std::string_view device, std::string_view param, T&& val) {
            std::string flat_key;
            flat_key.reserve(device.size() + 1 + param.size());
            flat_key.append(device).append(".").append(param);
            set_flat(flat_key, std::forward<T>(val));
        }

        // Upsert: updates the value for a combined key ("device.param") if the key exists;
        // otherwise adds a new one (slower than add() — linear search in vector mode)
        /**
        * @brief Upsert semantics: Updates the value if the key already exists, or adds a new one.
        * @note Complexity: in vector mode O(N) (linear duplicate search), in map mode O(1).
        * @param device Name of the device / domain
        * @param param  Name of the metric
        * @param val    New value to insert or update
        */
        void set_flat(std::string_view flat_key, const ParameterValue& val);
        void set_flat(std::string_view flat_key, ParameterValue&& val);
        template<typename T>
        void set_flat_impl(std::string_view flat_key, T&& val);

        // Strict update: updates only an existing key. Returns false if the key is not found (value remains unchanged)
        /**
        * @brief Strict update: Modifies the value ONLY if the key already exists in the container.
        * @note This method never increases the container size and never creates new entries.
        * @param device Name of the device / domain
        * @param param  Name of the metric
        * @param val    New value for the existing parameter
        * @return true — value successfully updated; false — such key not found (structure unchanged).
        */
        bool update(std::string_view device, std::string_view param, const ParameterValue& val);
        bool update(std::string_view device, std::string_view param, ParameterValue&& val);
        template<typename T>
        bool update_impl(std::string_view device, std::string_view param, T&& val) {
            std::string flat_key;
            flat_key.reserve(device.size() + 1 + param.size());
            flat_key.append(device).append(".").append(param);
            return update_flat(flat_key, std::forward<T>(val));
        }

        // Strict update: updates only an existing key (combined key "device.param").
        // Returns false if the key is not found (value remains unchanged).
        /**
        * @brief Strict update: Modifies the value ONLY if the key already exists in the container.
        * @note This method never increases the container size and never creates new entries.
        * @param device Name of the device / domain
        * @param param  Name of the metric
        * @param val    New value for the existing parameter
        * @return true — value successfully updated; false — such key not found (structure unchanged).
        */
        bool update_flat(std::string_view flat_key, const ParameterValue& val);
        bool update_flat(std::string_view flat_key, ParameterValue&& val);
        template<typename T>
        bool update_flat_impl(std::string_view flat_key, T&& val);
                		
		// Finding parameters without generating temporary std::string
		const ParameterValue* find(std::string_view device, std::string_view param) const noexcept;
		const ParameterValue* find_flat(std::string_view flat_key) const noexcept;

        void clear() noexcept;
        size_t size() const noexcept;

        // Quickly traverse all container elements
        using ConstCallback = void(*)(std::string_view flat_key, const ParameterValue& val, void* user_data);
        void iterate(ConstCallback callback, void* user_data) const;

        // Internal pImpl methods for MessagePack
        void pack(void* packer_ptr) const;
        void unpack(const void* object_ptr);

    private:
        void convert_to_map();

        bool is_flat_map{true};
        std::vector<std::pair<FlatKey, ParameterValue>> vector_storage; // CPU L1-cache line
		
		struct MapImpl;
        std::unique_ptr<MapImpl> map_storage; // Hidden tsl::robin_map
    };
}  // namespace msgframe