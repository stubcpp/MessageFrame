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
        ~HybridMessageMap() noexcept;

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
#ifndef NDEBUG
            if (find(device, param) != nullptr) {
                assert(false && "HybridMessageMap::add() called with duplicate key — use set() for upsert semantics");
            }
#endif
            if (is_vector_mode) {
                if (vector_storage.size() >= SMALL_CAPACITY) {
                    convert_to_map();
                } else {
                    // Stitching is done strictly in-place inside the ParameterKey constructor.
                    vector_storage.emplace_back(ParameterKey{device, param}, std::forward<T>(val));
                    return;
                }
            }

            if constexpr (std::is_rvalue_reference_v<T&&>) {
                map_emplace_rvalue(device, param, std::move(val));
            } else {
                map_emplace_lvalue(device, param, val);
            }
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
            if (is_vector_mode) {
                auto it = std::find_if(vector_storage.begin(), vector_storage.end(),
                                       [device, param](const auto& pair) {
                                           //Using the same fast logical comparison algorithm without gluing strings on the stack
                                           std::string_view l_view(pair.first.full_key);
                                           if (l_view.size() != device.size() + 1 + param.size()) return false;
                                           if (l_view.compare(0, device.size(), device) != 0) return false;
                                           if (l_view[device.size()] != '.') return false;
                                           return l_view.substr(device.size() + 1) == param;
                                       });
                if (it != vector_storage.end()) {
                    it->second = std::forward<T>(val);
                    return;
                }
            } else {
                auto* found_val = map_find_mutable(device, param);
                if (found_val != nullptr) {
                    *found_val = std::forward<T>(val);
                    return;
                }
            }
            add_impl(device, param, std::forward<T>(val));
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
            if (is_vector_mode) {
                auto it = std::find_if(vector_storage.begin(), vector_storage.end(),
                                       [device, param](const auto& pair) {
                                           std::string_view l_view(pair.first.full_key);
                                           if (l_view.size() != device.size() + 1 + param.size()) return false;
                                           if (l_view.compare(0, device.size(), device) != 0) return false;
                                           if (l_view[device.size()] != '.') return false;
                                           return l_view.substr(device.size() + 1) == param;
                                       });
                if (it != vector_storage.end()) {
                    it->second = std::forward<T>(val);
                    return true;
                }
            } else {
                auto* found_val = map_find_mutable(device, param);
                if (found_val != nullptr) {
                    *found_val = std::forward<T>(val);
                    return true;
                }
            }
            return false;
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
        void map_emplace_rvalue(std::string_view device, std::string_view param, ParameterValue&& val);
        void map_emplace_lvalue(std::string_view device, std::string_view param, const ParameterValue& val);
        ParameterValue* map_find_mutable(std::string_view device, std::string_view param) noexcept;

        bool is_vector_mode{true};
        std::vector<std::pair<ParameterKey, ParameterValue>> vector_storage; // CPU L1-cache line
		
		struct MapImpl;
        std::unique_ptr<MapImpl> map_storage; // Hidden tsl::robin_map
    };
}  // namespace msgframe
