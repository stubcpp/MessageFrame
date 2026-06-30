// examples/basic_usage.cpp
//
// Extended example of using the MessageFrame library.
// Goal: demonstrate the core API in a single file — creation, parameters,
// lookup, iteration, attachments, serialization, and deserialization.
// For performance benchmarks, see benchmarks/benchmark.cpp.

#include <messageframe/MessageFrame.hpp>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string_view>

// Strongly-typed message tags — use enums instead of raw integers.
// MyMsgId is the "catalog" of message kinds in your system.
// Each distinct message or command gets its own entry here.
// Think of it as your protocol's dispatch table.
enum class MyMsgId : int32_t {
    TELEMETRY_PACKET = 1001,
    COMMAND_PACKET = 1002
};

// MyMsgType is an orthogonal classification tag — it describes *how*
// the message should be treated (priority, urgency, semantics).
// The same MsgId can appear with different MsgTypes.
enum class MyMsgType : int32_t {
    PERIODIC = 1,
    CRITICAL = 2
};

// Callback for iterate_parameters() — invoked for each parameter
// in the message, without creating temporary std::string for the key.
void printParam(std::string_view flat_key, const msgframe::ParameterValue& val, void* /*user_data*/) {
    std::cout << "  [Iterate] " << flat_key << " = " << val.toString() << "\n";
}

int main() {
    // ----------------------------------------------------------------
    // 1. Create a message and configure its header
    // ----------------------------------------------------------------
    // The constructor accepts enums directly — internally cast to int32_t.
    msgframe::MessageFrame msg(
        MyMsgId::TELEMETRY_PACKET, // user-defined enum
        MyMsgType::CRITICAL,       // user-defined enum
        /*source_id=*/50,
        /*target_id=*/99,
        /*msg_cnt=*/1,
        /*proto_version=*/1,
        /*msg_flags=*/0x0001);

    // Header fields can be updated after construction
    msg.header().setFlags(0xAA00);
    msg.header().setMessageId(MyMsgId::COMMAND_PACKET);
    msg.header().setMessageType(MyMsgType::PERIODIC);
    msg.header().updateTimestamp(); // refresh to "now" right before transmission

    // ----------------------------------------------------------------
    // 2. Add parameters using the two-key API (device, parameter, value)
    // ----------------------------------------------------------------
    // Recommendation: keep combined key length (device + parameter)
    // within ~15–23 characters to benefit from SSO (Small String Optimization).
    msg.add("sensor_alpha", "voltage", msgframe::VALUE(12.6));       // "sensor_alpha.voltage" = 20 characters. Its Ok for SSO.
    msg.add("sensor_alpha", "status_ok", msgframe::VALUE(true));
    msg.add("device_core", "fw_version", msgframe::VALUE("v3.2.1"));
    msg.add("device_core", "error_codes", msgframe::VALUE(-5));

    std::cout << "Created message with " << msg.parameters_size() << " parameters.\n\n";

    // ----------------------------------------------------------------
    // 3. Lookup of a specific parameter without allocations (Zero-Allocation find)
    // ----------------------------------------------------------------
    std::cout << "Looking up sensor_alpha.voltage:\n";
    if (const auto* value = msg.find("sensor_alpha", "voltage")) {
        std::cout << "  found: " << value->toString() << "\n\n";
    }
    else {
        std::cout << "  not found\n\n";
    }

    // ----------------------------------------------------------------
    // 4. Iterate over all parameters via callback
    // ----------------------------------------------------------------
    std::cout << "All parameters:\n";
    msg.iterate_parameters(printParam, nullptr);

    // ----------------------------------------------------------------
    // 5. Attach a raw binary payload (e.g. IQ samples, spectrum snapshot)
    // ----------------------------------------------------------------
    std::vector<uint8_t> raw_iq_data = { 0x01, 0x02, 0x03, 0x04, 0x05, 0xAA, 0xBB, 0xCC };
    msg.add_attachment("raw_iq_stream", std::move(raw_iq_data));
    std::cout << "Total attachments: " << msg.get_attachments().size() << "\n\n";

    // ----------------------------------------------------------------
    // 6. Transport-agnostic serialization — write into a buffer
    // ----------------------------------------------------------------
    std::vector<uint8_t> send_buffer;
    msg.serialize(send_buffer);

    // ----------------------------------------------------------------
    // 7. On the receiving end: decode in place from raw bytes
    // ----------------------------------------------------------------
    msgframe::MessageFrame received;
    if (received.deserialize(send_buffer.data(), send_buffer.size())) {
        std::cout << "Decoded message with " << received.parameters_size() << " parameters.\n";
        if (received.header().getMessageType<MyMsgType>() == MyMsgType::PERIODIC) {
            std::cout << "Decoded message type: PERIODIC\n";
        }
        if (const auto* val = received.find("sensor_alpha", "voltage")) {
            auto voltage = val->tryGetDouble(); // voltage is std::optional

            // Option 1: Classic has_value() check
            if (voltage.has_value()) {
                double v = *voltage; // or voltage.value()
                std::cout << "Voltage (has_value): " << v << "\n";
            }

            // Option 2: value_or() with a default fallback
            double v2 = voltage.value_or(-1.0);
            std::cout << "Voltage (value_or): " << v2 << "\n";

            // Option 3: Shorthand if(optional)
            if (voltage) {
                std::cout << "Voltage (shorthand): " << *voltage << "\n";
            }

            // Additionally, you can use toString() for universal output
            std::cout << "Decoded sensor_alpha.voltage: " << val->toString() << "\n";
        }

    }

    return 0;
}
