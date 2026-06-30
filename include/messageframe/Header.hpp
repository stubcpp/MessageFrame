#pragma once
#include <cstdint>
#include <chrono>


namespace msgframe {

    class MessageHeader {
    public:
        MessageHeader() noexcept = default;

        // Universal Ctor that accepts any custom enum or integer types
        template <typename IdT, typename TypeT>
        MessageHeader(IdT msg_id, 
                      TypeT msg_type, 
                      uint32_t src_id, 
                      uint32_t tgt_id, 
                      uint64_t msg_cnt = 0,
                      uint16_t proto_version = 1,
                      uint16_t msg_flags = 0) noexcept
            : message_cnt(msg_cnt)
            , source_id(src_id)
            , target_id(tgt_id)
            , version(proto_version)
            , flags(msg_flags)
        {
            setMessageId(msg_id);
            setMessageType(msg_type);
            updateTimestamp();
        }

        // Getters
        uint16_t getVersion() const noexcept { return version; }
        uint16_t getFlags() const noexcept { return flags; }
        uint64_t getMessageCounter() const noexcept { return message_cnt; }
        uint64_t getTimestamp() const noexcept { return timestamp; }
        uint32_t getSourceID() const noexcept { return source_id; }
        uint32_t getTargetID() const noexcept { return target_id; }

        // Returns the raw int32_t message ID
        int32_t getMessageIdRaw() const noexcept { return message_id; }

        // Returns the message ID, automatically casting it to the user's enum
        template <typename IdT>
		IdT getMessageId() const noexcept {return static_cast<IdT>(message_id); }

        // Returns the raw int32_t message type
        int32_t getMessageTypeRaw() const noexcept { return message_type; }

        // Returns the message type, automatically casting it to the user's enum
        template <typename TypeT>
        TypeT getMessageType() const noexcept {return static_cast<TypeT>(message_type);}

        // Setters
        void setVersion(uint16_t proto_version) noexcept { version = proto_version; }
        void setFlags(uint16_t msg_flags) noexcept { flags = msg_flags; }
        void setMessageCounter(uint64_t cnt) noexcept { message_cnt = cnt; }
        void setTimestamp(uint64_t ts) noexcept { timestamp = ts; }
        void setSourceID(uint32_t src_id) noexcept { source_id = src_id; }
        void setTargetID(uint32_t tgt_id) noexcept { target_id = tgt_id; }

        void updateTimestamp() noexcept {timestamp = get_timestamp_ms();}

        template <typename IdT>
		void setMessageId(IdT id) noexcept {message_id = static_cast<int32_t>(id);}

        template <typename TypeT>
		void setMessageType(TypeT type) noexcept {message_type = static_cast<int32_t>(type);}

        // methods for MessagePack serialization
        void pack(void* packer_ptr) const;
        void unpack(const void* object_ptr);

    private:
        static uint64_t get_timestamp_ms() noexcept {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count()
            );
        }

        // Fields are aligned on the 64/32 bit boundary to avoid performance degradation (Padding)
        uint64_t timestamp{get_timestamp_ms()}; // 8 bytes (Timestamp, ms)
		uint64_t message_cnt{0};                // 8 bytes (Сurrent message number)
		uint32_t source_id{0};                  // 4 bytes (Sender ID)
		uint32_t target_id{0};                  // 4 bytes (Receiver ID)
		int32_t message_id{0};                  // 4 bytes (Custom MSG_ID) 
		int32_t message_type{0};                // 4 bytes (Custom MSG_TYPE)
		uint16_t version{1};                    // 2 bytes (Custom Protocol version)
        uint16_t flags{0};                      // 2 bytes (Custom bit flags)
    };

} // namespace msgframe
