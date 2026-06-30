#include "messageframe/Header.hpp"
#include <msgpack.hpp>

namespace msgframe {

    class VectorBuffer {
    public:
        VectorBuffer(std::vector<uint8_t>& buffer) : m_buf(buffer) {}
        void write(const char* buf, size_t len) {
            size_t old_size = m_buf.size();
            m_buf.resize(old_size + len);
            std::memcpy(m_buf.data() + old_size, buf, len);
        }
    private:
        std::vector<uint8_t>& m_buf;
    };

    void MessageHeader::pack(void* packer_ptr) const {
        if (!packer_ptr) return;

        auto* pk = static_cast<msgpack::packer<VectorBuffer>*>(packer_ptr);

        pk->pack_array(8);
        pk->pack(timestamp);
        pk->pack(message_cnt);
        pk->pack(source_id);
        pk->pack(target_id);
        pk->pack(message_id);
        pk->pack(message_type);
        pk->pack(version);
        pk->pack(flags);
    }

    void MessageHeader::unpack(const void* object_ptr) {
        if (!object_ptr) return;

        const auto& obj = *static_cast<const msgpack::object*>(object_ptr);

        if (obj.type != msgpack::type::ARRAY || obj.via.array.size < 8) {
            return;
        }

        const auto* ptr = obj.via.array.ptr;
        timestamp    = ptr[0].as<uint64_t>();
        message_cnt  = ptr[1].as<uint64_t>();
        source_id    = ptr[2].as<uint32_t>();
        target_id    = ptr[3].as<uint32_t>();
        message_id   = ptr[4].as<int32_t>();
        message_type = ptr[5].as<int32_t>();
        version      = ptr[6].as<uint16_t>();
        flags        = ptr[7].as<uint16_t>();
    }

} // namespace msgframe

