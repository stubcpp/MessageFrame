// tests/test_message_frame.cpp
#include "test_framework.hpp"
#include <messageframe/MessageFrame.hpp>
#include <vector>
#include <cstring>

using msgframe::MessageFrame;
using msgframe::ParameterValue;

// --------------------------------------------------------------------
// Group: Serialization — Checking binary packing and heap-reuse
// --------------------------------------------------------------------

TEST(Serialization, EndToEndPackUnpackWithHeavyAttachments) {
    MessageFrame source_frame;

	// Fill with metadata
	source_frame.add("system", "status", ParameterValue(std::string("operational")));
	source_frame.add("sensor", "reading", ParameterValue(3.14159));
	
	// Add a heavy raw binary attachment (e.g., an array of raw signals from an SDR)
	std::vector<uint8_t> mock_signal(2048, 0xAB);
	source_frame.add_attachment("raw_sdr_channel_A", std::move(mock_signal));
	
	// Serialize to a reusable buffer
	std::vector<uint8_t> buffer;
	source_frame.serialize(buffer);
	CHECK(buffer.size() > 0);
	
	// Deserialize to a completely new frame
	MessageFrame target_frame;
	bool unpack_ok = target_frame.deserialize(buffer.data(), buffer.size());
	CHECK(unpack_ok);
	
	// Check metadata consistency after network unpacking
    const auto* status = target_frame.find("system", "status");
    CHECK_NOT_NULL(status);
    if (status) {
        auto s = status->tryGetString();
        CHECK(s.has_value());
        if (s) CHECK_EQ(*s, std::string("operational"));
    }

    // Checking the preservation of binary attachments via vector indices
    const auto& target_attachments = target_frame.get_attachments();
    CHECK_EQ(target_attachments.size(), static_cast<size_t>(1));
    if (!target_attachments.empty()) {
        CHECK_EQ(target_attachments[0].name, std::string("raw_sdr_channel_A"));
        CHECK_EQ(target_attachments[0].raw_data.size(), static_cast<size_t>(2048));

        // Element-by-element inspection of buffer bytes
        CHECK_EQ(target_attachments[0].raw_data[0], static_cast<uint8_t>(0xAB));
        CHECK_EQ(target_attachments[0].raw_data[2047], static_cast<uint8_t>(0xAB));
    }
}

TEST(Serialization, CapacityRetentionOnConsecutiveSerializations) {
    MessageFrame msg;
    msg.add("dev", "p", ParameterValue(static_cast<int64_t>(42)));

    std::vector<uint8_t> shared_network_buffer;
    shared_network_buffer.reserve(4096); // Pool pre-allocation

    msg.serialize(shared_network_buffer);
    size_t cap_after_first = shared_network_buffer.capacity();

    // Check that VectorBuffer has not broken the capacity of our network pool
    CHECK(cap_after_first >= static_cast<size_t>(4096));

    // A second serialization to the same buffer should not provoke new allocations.
    msg.serialize(shared_network_buffer);
    CHECK_EQ(shared_network_buffer.capacity(), cap_after_first);
}

TEST(Serialization, MalformedBinaryBufferReturnsFalseGracefully) {
    MessageFrame msg;
    // Feed the deserializer random binary garbage instead of MessagePack
    std::vector<uint8_t> garbage(50, 0xFE);

    bool result = msg.deserialize(garbage.data(), garbage.size());

    // The program should not crash on a Segmentation Fault; unpack should return false
    CHECK_FALSE(result);
}

/*
int main() {
    return msgframe_test::run_all();
}
*/