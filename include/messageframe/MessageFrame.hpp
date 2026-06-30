// MESSAGE FRAME MAIN CLASS
#pragma once
#include "messageframe/Structures.hpp"
#include "messageframe/Header.hpp"   
#include "messageframe/Value.hpp"  
#include "messageframe/HybridMessageMap.hpp"
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <utility>


namespace msgframe {
	
class MessageFrame {
    public:
        MessageFrame() = default;

        // Ctor for fast initialization
        template <typename IdT, typename TypeT>
        MessageFrame(IdT msg_id, TypeT msg_type, uint32_t src_id, uint32_t tgt_id,
            uint64_t msg_cnt = 0, uint16_t proto_version = 1, uint16_t msg_flags = 0) noexcept
            : m_header(msg_id, msg_type, src_id, tgt_id, msg_cnt, proto_version, msg_flags) {
        }

        // Direct access to the header
        MessageHeader& header() noexcept { return m_header; }
        const MessageHeader& header() const noexcept { return m_header; }

        // ----------------------------------------------------------------
        // Proxy methods for HybridMessageMap (Redirect to parameters)
        //
        // Each method is duplicated in lvalue (const ParameterValue&) and rvalue
        // (ParameterValue&&) versions — exactly as in HybridMessageMap.
        // This is intentional: a single by-value overload would look simpler, but
        // would force the compiler to COPY ParameterValue for lvalue arguments
        // (param passed by value -> always copy/move-constructed), which breaks
        // the Zero-Allocation guarantee of the library at the public MessageFrame facade.
        // ----------------------------------------------------------------
        
        // CRITICAL. Repeated add() with the same key will create a duplicate entry in vector mode
        // (undefined behavior for subsequent find — it will return the first) and will lead to
        // incorrect behavior. Use set() for upsert semantics.
        /**
        * @brief Adds a new parameter without checking for duplicates (Maximum speed).
        * @note Complexity: O(1) in vector mode (proxy to HybridMessageMap::add).
        * @warning If the key already exists, in Release build a duplicate will be created
        *          (find() will return the first). In Debug build an assert will trigger.
        *          Use set() for "insert or update" logic.
        * @param device Name of the device / domain (e.g., "sensor_alpha")
        * @param param  Name of the metric (e.g., "voltage")
        * @param val    Parameter value
        */
        void add(std::string_view device, std::string_view param, const ParameterValue& val) {
            parameters.add(device, param, val);
        }
        void add(std::string_view device, std::string_view param, ParameterValue&& val) {
            parameters.add(device, param, std::move(val));
        }

        /**
        * @brief Adds a new parameter when the key is already combined ("device.parameter")
        *        without checking for duplicates (Maximum speed).
        * @note Complexity: O(1) in vector mode (proxy to HybridMessageMap::add_flat).
        * @warning If the key already exists, in Release build a duplicate will be created
        *          (find_flat() will return the first). In Debug build an assert will trigger.
        *          Use set_flat() for "insert or update" logic.
        * @param flat_key Combined key (e.g., "device.parameter")
        * @param val      Parameter value
        */
        void add_flat(std::string_view flat_key, const ParameterValue& val) {
            parameters.add_flat(flat_key, val);
        }
        void add_flat(std::string_view flat_key, ParameterValue&& val) {
            parameters.add_flat(flat_key, std::move(val));
        }

        /**
        * @brief Upsert semantics: Updates the value if the key already exists, or adds a new one.
        * @note Complexity: in vector mode O(N) (linear duplicate search), in map mode O(1)
        *       (proxy to HybridMessageMap::set).
        * @param device Name of the device / domain
        * @param param  Name of the metric
        * @param val    New value to insert or update
        */
        void set(std::string_view device, std::string_view param, const ParameterValue& val) {
            parameters.set(device, param, val);
        }
        void set(std::string_view device, std::string_view param, ParameterValue&& val) {
            parameters.set(device, param, std::move(val));
        }

        /**
        * @brief Upsert semantics: Updates the value if the key already exists, or adds a new one
        *        (combined key "device.param").
        * @note Complexity: in vector mode O(N) (linear duplicate search), in map mode O(1)
        *       (proxy to HybridMessageMap::set_flat).
        * @param flat_key Combined key (e.g., "device.parameter")
        * @param val      New value to insert or update
        */
        void set_flat(std::string_view flat_key, const ParameterValue& val) {
            parameters.set_flat(flat_key, val);
        }
        void set_flat(std::string_view flat_key, ParameterValue&& val) {
            parameters.set_flat(flat_key, std::move(val));
        }

        /**
        * @brief Strict update: Modifies the value ONLY if the key already exists in the container.
        * @note This method never increases the number of parameters and never creates new entries
        *       (proxy to HybridMessageMap::update).
        * @param device Name of the device / domain
        * @param param  Name of the metric
        * @param val    New value for the existing parameter
        * @return true — value successfully updated; false — such key not found (structure unchanged).
        */
        bool update(std::string_view device, std::string_view param, const ParameterValue& val) {
            return parameters.update(device, param, val);
        }
        bool update(std::string_view device, std::string_view param, ParameterValue&& val) {
            return parameters.update(device, param, std::move(val));
        }

        /**
        * @brief Strict update: Modifies the value ONLY if the key already exists in the container
        *        (combined key "device.param").
        * @note This method never increases the number of parameters and never creates new entries
        *       (proxy to HybridMessageMap::update_flat).
        * @param flat_key Combined key (e.g., "device.parameter")
        * @param val      New value for the existing parameter
        * @return true — value successfully updated; false — such key not found (structure unchanged).
        */
        bool update_flat(std::string_view flat_key, const ParameterValue& val) {
            return parameters.update_flat(flat_key, val);
        }
        bool update_flat(std::string_view flat_key, ParameterValue&& val) {
            return parameters.update_flat(flat_key, std::move(val));
        }

        /**
        * @brief Finds a parameter without creating a temporary std::string (Zero-Allocation find).
        * @note Complexity: O(N) in vector mode (linear comparison), O(1) in map mode.
        * @param device Name of the device / domain
        * @param param  Name of the metric
        * @return Pointer to the found value, or nullptr if the key is not found.
        */
        const ParameterValue* find(std::string_view device, std::string_view param) const noexcept {
            return parameters.find(device, param);
        }

        /**
        * @brief Finds a parameter by its combined key ("device.param") without creating
        *        a temporary std::string (Zero-Allocation find).
        * @note Complexity: O(N) in vector mode (linear comparison), O(1) in map mode.
        * @param flat_key Combined key (e.g., "device.parameter")
        * @return Pointer to the found value, or nullptr if the key is not found.
        */
        const ParameterValue* find_flat(std::string_view flat_key) const noexcept {
            return parameters.find_flat(flat_key);
        }

        /**
        * @brief Adds a large binary attachment to the message.
        * @note Attachments bypass the parameter map — bulk data is stored directly.
        * @param name Logical name of the attachment (e.g., "raw_iq_stream").
        * @param data Binary payload to attach (moved into the container).
        */
        void add_attachment(std::string name, std::vector<uint8_t> data);

        /**
         * @brief Provides read-only access to all attachments of the message.
         * @return Const reference to the vector of attachments.
         */
        const std::vector<Attachment>& get_attachments() const noexcept { return attachments; }

        /**
        * @brief Returns the current number of parameters in the message.
        * @return Count of parameters stored in the container.
        */
        size_t parameters_size() const noexcept { return parameters.size(); }

        /**
         * @brief Provides an iterator-style traversal of all parameters.
         * @note Invokes the given callback for each parameter without extra allocations.
         * @param callback Function pointer called for every parameter.
         * @param user_data Optional user context passed to the callback.
         */
        void iterate_parameters(HybridMessageMap::ConstCallback callback, void* user_data) const {
            parameters.iterate(callback, user_data);
        }

        /**
        * @brief Clears the message for reusing the same object.
        * @note Does not release the allocated capacity of the parameter container —
        *       repeated clear()+add() cycles within the same element count do not
        *       trigger new allocations. Always resets the container back to vector mode,
        *       even if it previously switched to map mode.
        * @warning Always call clear() before refilling the same MessageFrame object —
        *          otherwise add() will append new parameters to the existing ones
        *          (duplicates), instead of replacing the previous set. If you create
        *          a new MessageFrame for each message, clear() is not needed.
        */
        void clear() noexcept;

        // Network serialization
        void serialize(std::vector<uint8_t>& output_buffer) const;
        bool deserialize(const uint8_t* data, size_t size);

    private:
        MessageHeader m_header; 
        HybridMessageMap parameters;
        std::vector<Attachment> attachments;
    };

} // namespace msgframe