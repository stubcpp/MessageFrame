#include "test_framework.hpp"
#include <messageframe/MessageFrame.hpp>

using msgframe::MessageFrame;
using msgframe::ParameterValue;

TEST(MessageFrameProxy, AddLvalueIsFoundByDeviceParam) {
    MessageFrame msg;
    ParameterValue v(12.6);
    msg.add("sensor_alpha", "voltage", v);

    const auto* found = msg.find("sensor_alpha", "voltage");
    CHECK_NOT_NULL(found);
}

TEST(MessageFrameProxy, AddRvalueIsFound) {
    MessageFrame msg;
    msg.add("sensor_alpha", "voltage", ParameterValue(1.0));

    CHECK_NOT_NULL(msg.find("sensor_alpha", "voltage"));
}

TEST(MessageFrameProxy, AddFlatLvalueAndRvalue) {
    MessageFrame msg;
    ParameterValue v(1.0);
    msg.add_flat("dev.p1", v);
    msg.add_flat("dev.p2", ParameterValue(2.0));

    CHECK_NOT_NULL(msg.find_flat("dev.p1"));
    CHECK_NOT_NULL(msg.find_flat("dev.p2"));
}

TEST(MessageFrameProxy, SetUpsertDoesNotDuplicate) {
    MessageFrame msg;
    msg.add("dev", "voltage", ParameterValue(1.0));
    msg.set("dev", "voltage", ParameterValue(9.0));

    CHECK_EQ(msg.parameters_size(), static_cast<size_t>(1));
}

TEST(MessageFrameProxy, UpdateExistingReturnsTrueMissingReturnsFalse) {
    MessageFrame msg;
    msg.add("dev", "voltage", ParameterValue(1.0));

    bool ok_lvalue = msg.update("dev", "voltage", ParameterValue(2.0));
    bool ok_rvalue = msg.update("dev", "voltage", ParameterValue(3.0));
    bool missing = msg.update("dev", "ghost", ParameterValue(4.0));

    CHECK(ok_lvalue);
    CHECK(ok_rvalue);
    CHECK_FALSE(missing);
}

TEST(MessageFrameProxy, UpdateFlatMirrorsUpdate) {
    MessageFrame msg;
    msg.add_flat("dev.voltage", ParameterValue(1.0));

    bool ok = msg.update_flat("dev.voltage", ParameterValue(5.0));
    bool missing = msg.update_flat("dev.ghost", ParameterValue(5.0));

    CHECK(ok);
    CHECK_FALSE(missing);
}

int main() { return msgframe_test::run_all(); }
