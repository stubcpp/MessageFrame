#include "messageframe/MessageFrame.hpp"
#include "messageframe/Structures.hpp"
#include "msgpack.hpp"
#include <algorithm>
#include <cstring>


namespace msgframe {
	
    void MessageFrame::add_attachment(std::string name, std::vector<uint8_t> data) {
        attachments.push_back({ std::move(name), std::move(data) });
    }

    void MessageFrame::clear() noexcept {
        m_header.updateTimestamp();
        parameters.clear();
        attachments.clear();
    }

    // --- Серіалізація всього кадру (Header + Parameters + Attachments) ---
    void MessageFrame::serialize(std::vector<uint8_t>& output_buffer) const {
        output_buffer.clear();
        VectorBuffer vbuf(output_buffer);
        msgpack::packer<VectorBuffer> pk(&vbuf);

        // Структура верхнього рівня: Фіксований масив із 3 елементів [Header, Parameters, Attachments]
        pk.pack_array(3);

        // 1. Пакуємо заголовок
        m_header.pack(&pk);

        // 2. Пакуємо мапу параметрів
        parameters.pack(&pk);

        // 3. Пакуємо бінарні атачменти
        pk.pack_array(attachments.size());
        for (const auto& attach : attachments) {
            pk.pack_array(2); // Пара: [Ім'я, Дані]
            pk.pack(attach.name);
            
            // Запис великого масиву байтів у MessagePack форматі BIN
            pk.pack_bin(attach.raw_data.size());
            pk.pack_bin_body(reinterpret_cast<const char*>(attach.raw_data.data()), attach.raw_data.size());
        }
    }

    // --- Десеріалізація всього кадру з сирого буфера мережі ---
    bool MessageFrame::deserialize(const uint8_t* data, size_t size) {
        if (!data || size == 0) return false;

        try {
            // Розбір MessagePack дерева об'єктів «на льоту»
            msgpack::object_handle oh = msgpack::unpack(reinterpret_cast<const char*>(data), size);
            msgpack::object const& root_obj = oh.get();

            // Кореневий елемент повинен бути масивом з 3 елементів
            if (root_obj.type != msgpack::type::ARRAY || root_obj.via.array.size < 3) {
                return false;
            }

            const auto* root_ptr = root_obj.via.array.ptr;

            // 1. Десеріалізація хедера
			m_header.unpack(&root_ptr[0]);
			
			// 2. Десеріалізація параметрів
			parameters.unpack(&root_ptr[1]);
			
			// 3. Десеріалізація атачментів
			attachments.clear();
			
			const auto& attach_arr_obj = root_ptr[2];
			if (attach_arr_obj.type == msgpack::type::ARRAY) {
				size_t attach_count = attach_arr_obj.via.array.size;
				attachments.reserve(attach_count);
				const auto* attach_ptr = attach_arr_obj.via.array.ptr;
				
				for (size_t i = 0; i < attach_count; ++i) {
					if (attach_ptr[i].type == msgpack::type::ARRAY && attach_ptr[i].via.array.size >= 2) {
						const auto* pair_ptr = attach_ptr[i].via.array.ptr;
						Attachment attach;
						attach.name = pair_ptr[0].as<std::string>(); 
						
						// Читання бінарних даних
						if (pair_ptr[1].type == msgpack::type::BIN) {
							size_t bin_size = pair_ptr[1].via.bin.size;
							const char* bin_data = pair_ptr[1].via.bin.ptr;
							attach.raw_data.resize(bin_size);
							std::memcpy(attach.raw_data.data(), bin_data, bin_size);
						}
						
						attachments.push_back(std::move(attach));
					}
				}
			}
			
			return true;
		} catch (const std::exception&) {
			// Захист від падіння у випадку передачі пошкодженого бінарного пакету
			return false;
		}
	}
} // namespace msgframe