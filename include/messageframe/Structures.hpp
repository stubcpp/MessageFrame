#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <functional>


namespace msgframe {
	
	// Optimized flat key for vector. Protects against double allocations
    struct FlatKey {
        std::string full_key; // Format: "device.parameter"
        bool operator==(const FlatKey& other) const noexcept {
            return full_key == other.full_key; // Формат: "device.parameter"
        }
    };

	// Large raw binary data (IQ-ether, files)
	struct Attachment {
		std::string name;
		std::vector<uint8_t> raw_data;
	};

    // Спеціальний адаптер, який дозволяє msgpack::packer писати НАПРЯМУ в std::vector
    // Це повністю прибирає проміжний sbuffer та усуває зайвий malloc/memcpy!
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

    // is_transparent — тег, що каже tsl::robin_map: "цей функтор уміє
    // приймати кілька сумісних типів ключа, не лише Key". Потрібен власний хеш,
    // бо std::hash<std::string> не приймає std::string_view (no conversion,
    // це навмисне обмеження стандарту — без цього кожен hash() міг би мовчки
    // алокувати std::string). std::equal_to<> вже з коробки прозорий (має
    // is_transparent і шаблонний operator()), його писати самим не треба.
    struct StringViewHash {
        using is_transparent = void;
        size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }
    };
}