// tests/test_messageframe_parameter_api.cpp
#include "test_framework.hpp"
#include <messageframe/MessageFrame.hpp>
#include <string>
#include <vector>

using msgframe::FlatKey;
using msgframe::MessageFrame;
using msgframe::ParameterValue;

// --------------------------------------------------------------------
// Group: MessageFrameParameterApi — add()/find() over the two-key API
// --------------------------------------------------------------------

TEST(MessageFrameParameterApi, AddLvalueIsFoundByDeviceParam) {
    MessageFrame msg;
    ParameterValue v(12.6);
    msg.add("sensor_alpha", "voltage", v);

    const auto* found = msg.find("sensor_alpha", "voltage");
    CHECK_NOT_NULL(found);
}

TEST(MessageFrameParameterApi, AddRvalueIsFound) {
    MessageFrame msg;
    msg.add("sensor_alpha", "voltage", ParameterValue(1.0));

    CHECK_NOT_NULL(msg.find("sensor_alpha", "voltage"));
}

// --------------------------------------------------------------------
// Group: MessageFrameParameterApiFlat — add_flat()/find_flat() via FlatKey
//
// FlatKey::compose(device, param) is the ONLY way to build a key for
// these methods — add_flat()/find_flat() no longer accept a raw string
// at all, so the old failure mode (wrong separator, missing separator,
// silently misfiled entry) is now a compile error, not a runtime bug.
// See FlatKeyValidation tests in tests/test_flat_key.cpp for direct
// coverage of FlatKey itself (compose/copy/move semantics); this group
// covers it through the MessageFrame parameter-access layer specifically.
// --------------------------------------------------------------------

TEST(MessageFrameParameterApiFlat, AddFlatLvalueAndRvalue) {
    MessageFrame msg;
    FlatKey k1 = FlatKey::compose("dev", "p1");
    ParameterValue v(1.0);
    msg.add_flat(k1, v);
    msg.add_flat(FlatKey::compose("dev", "p2"), ParameterValue(2.0));

    CHECK_NOT_NULL(msg.find_flat(k1));
    CHECK_NOT_NULL(msg.find_flat(FlatKey::compose("dev", "p2")));
}

TEST(MessageFrameParameterApiFlat, RoundTripsThroughTwoKeyAndFlatAPIsConsistently) {
    // A value written via add() must be reachable via find_flat() with a
    // FlatKey built from the same device/param, and vice versa — the two
    // APIs must agree on identity.
    MessageFrame msg;
    msg.add("dev", "p1", ParameterValue(7.0));

    const auto* via_flat = msg.find_flat(FlatKey::compose("dev", "p1"));
    CHECK_NOT_NULL(via_flat);

    msg.add_flat(FlatKey::compose("dev", "p2"), ParameterValue(8.0));
    const auto* via_two_key = msg.find("dev", "p2");
    CHECK_NOT_NULL(via_two_key);
}

// --------------------------------------------------------------------
// Group: MessageFrameParameterApiUpsert — set()/set_flat() upsert semantics
// --------------------------------------------------------------------

TEST(MessageFrameParameterApiUpsert, SetUpsertDoesNotDuplicate) {
    MessageFrame msg;
    msg.add("dev", "voltage", ParameterValue(1.0));
    msg.set("dev", "voltage", ParameterValue(9.0));

    CHECK_EQ(msg.parameters_size(), static_cast<size_t>(1));
}

TEST(MessageFrameParameterApiUpsert, SetLvalueAndRvalueBothUpsert) {
    MessageFrame msg;
    msg.add("dev", "voltage", ParameterValue(1.0));

    ParameterValue lvalue_update(2.0);
    msg.set("dev", "voltage", lvalue_update);          // lvalue overload
    msg.set("dev", "voltage", ParameterValue(3.0));    // rvalue overload

    CHECK_EQ(msg.parameters_size(), static_cast<size_t>(1));
    const auto* v = msg.find("dev", "voltage");
    CHECK_NOT_NULL(v);
    if (v) {
        auto d = v->tryGetDouble();
        CHECK(d.has_value());
        if (d) CHECK_EQ(*d, 3.0);
    }
}

TEST(MessageFrameParameterApiUpsert, SetFlatUpsertDoesNotDuplicate) {
    MessageFrame msg;
    FlatKey key = FlatKey::compose("dev", "voltage");
    msg.set_flat(key, ParameterValue(1.0));
    msg.set_flat(key, ParameterValue(9.0));

    CHECK_EQ(msg.parameters_size(), static_cast<size_t>(1));
    const auto* v = msg.find_flat(key);
    CHECK_NOT_NULL(v);
    if (v) {
        auto d = v->tryGetDouble();
        CHECK(d.has_value());
        if (d) CHECK_EQ(*d, 9.0);
    }
}

// --------------------------------------------------------------------
// Group: MessageFrameParameterApiStrictUpdate — update()/update_flat() — never
// creates new entries.
// --------------------------------------------------------------------

TEST(MessageFrameParameterApiStrictUpdate, UpdateLvalueAndRvalueSucceedMissingKeyFails) {
    MessageFrame msg;
    msg.add("dev", "voltage", ParameterValue(1.0));

    ParameterValue lvalue_update(2.0);
    bool ok_lvalue = msg.update("dev", "voltage", lvalue_update);       // lvalue overload
    bool ok_rvalue = msg.update("dev", "voltage", ParameterValue(3.0)); // rvalue overload
    bool missing   = msg.update("dev", "ghost", ParameterValue(4.0));

    CHECK(ok_lvalue);
    CHECK(ok_rvalue);
    CHECK_FALSE(missing);
    // A failed update() on a missing key must not create it.
    CHECK_EQ(msg.parameters_size(), static_cast<size_t>(1));
}

TEST(MessageFrameParameterApiStrictUpdate, UpdateFlatLvalueAndRvalueMirrorsUpdate) {
    MessageFrame msg;
    FlatKey key = FlatKey::compose("dev", "voltage");
    msg.add_flat(key, ParameterValue(1.0));

    ParameterValue lvalue_update(5.0);
    bool ok_lvalue = msg.update_flat(key, lvalue_update);
    bool ok_rvalue = msg.update_flat(key, ParameterValue(6.0));
    bool missing   = msg.update_flat(FlatKey::compose("dev", "ghost"), ParameterValue(5.0));

    CHECK(ok_lvalue);
    CHECK(ok_rvalue);
    CHECK_FALSE(missing);
    CHECK_EQ(msg.parameters_size(), static_cast<size_t>(1));
}

// --------------------------------------------------------------------
// Group: MessageFrameIteration — iterate_parameters() callback traversal
// --------------------------------------------------------------------

namespace {
struct IterationState {
    size_t visits = 0;
    bool saw_dev_p1 = false;
    bool saw_dev_p2 = false;
};

void count_and_check(std::string_view flat_key, const ParameterValue& val, void* user_data) {
    auto* state = static_cast<IterationState*>(user_data);
    ++state->visits;
    // Compare against the same FlatKey construction used to write the data,
    // instead of hand-rolling the separator in the test.
    if (flat_key == FlatKey::compose("dev", "p1").view()) {
        state->saw_dev_p1 = true;
        auto d = val.tryGetDouble();
        if (d && *d == 1.0) { /* value matches */ }
    }
    if (flat_key == FlatKey::compose("dev", "p2").view()) {
        state->saw_dev_p2 = true;
    }
}
} // namespace

TEST(MessageFrameIteration, IterateParametersVisitsEveryEntryExactlyOnce) {
    MessageFrame msg;
    msg.add("dev", "p1", ParameterValue(1.0));
    msg.add("dev", "p2", ParameterValue(2.0));

    IterationState state;
    msg.iterate_parameters(&count_and_check, &state);

    CHECK_EQ(state.visits, msg.parameters_size());
    CHECK_EQ(state.visits, static_cast<size_t>(2));
    CHECK(state.saw_dev_p1);
    CHECK(state.saw_dev_p2);
}

// --------------------------------------------------------------------
// Group: MessageFrameLifecycle — clear() resets params/attachments but
// preserves the header, matching the documented reuse contract.
// --------------------------------------------------------------------

TEST(MessageFrameLifecycle, ClearRemovesParametersAndAttachmentsButKeepsHeader) {
    MessageFrame msg(/*msg_id=*/int32_t(77), /*msg_type=*/int32_t(1),
                      /*src_id=*/10, /*tgt_id=*/20);
    msg.add("dev", "p1", ParameterValue(1.0));
    msg.add_attachment("blob", std::vector<uint8_t>{1, 2, 3});

    CHECK_EQ(msg.parameters_size(), static_cast<size_t>(1));
    CHECK_EQ(msg.get_attachments().size(), static_cast<size_t>(1));

    msg.clear();

    CHECK_EQ(msg.parameters_size(), static_cast<size_t>(0));
    CHECK_EQ(msg.get_attachments().size(), static_cast<size_t>(0));
    // clear() is documented as "reusing the same object" — it must not
    // reset the header, otherwise callers would have to re-set routing
    // info after every clear()+refill cycle.
    CHECK_EQ(msg.header().getMessageIdRaw(), static_cast<int32_t>(77));

    // Confirm the object is actually reusable after clear().
    msg.add("dev", "p1_again", ParameterValue(9.0));
    CHECK_EQ(msg.parameters_size(), static_cast<size_t>(1));
}

int main() { return msgframe_test::run_all(); }
