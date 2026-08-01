// tests/test_message_frame.cpp
#include "test_framework.hpp"
#include <messageframe/MessageFrame.hpp>
#include <vector>
#include <cstring>

using msgframe::MessageFrame;
using msgframe::ParameterValue;

// --------------------------------------------------------------------
// Group: Serialization — Перевірка бінарного пакінгу та heap-reuse
// --------------------------------------------------------------------

TEST(Serialization, EndToEndPackUnpackWithHeavyAttachments) {
    MessageFrame source_frame;

    // Наповнюємо метаданими
    source_frame.add("system", "status", ParameterValue(std::string("operational")));
    source_frame.add("sensor", "reading", ParameterValue(3.14159));

    // Додаємо важкий сирий бінарний атачмент (наприклад, масив сирих сигналів з SDR)
    std::vector<uint8_t> mock_signal(2048, 0xAB);
    source_frame.add_attachment("raw_sdr_channel_A", std::move(mock_signal));

    // Серіалізація в повторно використовуваний буфер
    std::vector<uint8_t> buffer;
    source_frame.serialize(buffer);
    CHECK(buffer.size() > 0);

    // Десеріалізація в абсолютно новий фрейм
    MessageFrame target_frame;
    bool unpack_ok = target_frame.deserialize(buffer.data(), buffer.size());
    CHECK(unpack_ok);

    // Перевірка консистентності метаданих після мережевого розпакування
    const auto* status = target_frame.find("system", "status");
    CHECK_NOT_NULL(status);
    if (status) {
        auto s = status->tryGetString();
        CHECK(s.has_value());
        if (s) CHECK_EQ(*s, std::string("operational"));
    }

    // Перевірка збереження бінарних атачментів через індекси вектора
    const auto& target_attachments = target_frame.get_attachments();
    CHECK_EQ(target_attachments.size(), static_cast<size_t>(1));
    if (!target_attachments.empty()) {
        CHECK_EQ(target_attachments[0].name, std::string("raw_sdr_channel_A"));
        CHECK_EQ(target_attachments[0].raw_data.size(), static_cast<size_t>(2048));

        // Поелементна перевірка байтів буфера
        CHECK_EQ(target_attachments[0].raw_data[0], static_cast<uint8_t>(0xAB));
        CHECK_EQ(target_attachments[0].raw_data[2047], static_cast<uint8_t>(0xAB));
    }
}

TEST(Serialization, CapacityRetentionOnConsecutiveSerializations) {
    MessageFrame msg;
    msg.add("dev", "p", ParameterValue(42));

    std::vector<uint8_t> shared_network_buffer;
    shared_network_buffer.reserve(4096); // Пре-аллокація пулу

    msg.serialize(shared_network_buffer);
    size_t cap_after_first = shared_network_buffer.capacity();

    // Перевіряємо, що VectorBuffer не зламав capacity нашого мережевого пулу
    CHECK(cap_after_first >= static_cast<size_t>(4096));

    // Друга серіалізація в цей самий буфер не повинна провокувати нові алокації
    msg.serialize(shared_network_buffer);
    CHECK_EQ(shared_network_buffer.capacity(), cap_after_first);
}

TEST(Serialization, MalformedBinaryBufferReturnsFalseGracefully) {
    MessageFrame msg;
    // Підсовуємо десеріалізатору випадкове бінарне сміття замість MessagePack
    std::vector<uint8_t> garbage(50, 0xFE);

    bool result = msg.deserialize(garbage.data(), garbage.size());

    // Програма не повинна падати по Segmentation Fault; unpack має повернути false
    CHECK_FALSE(result);
}

int main() {
    return msgframe_test::run_all();
}
