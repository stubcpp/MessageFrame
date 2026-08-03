// tests/test_hybrid_map.cpp
#include "test_framework.hpp"
#include <messageframe/HybridMessageMap.hpp>
#include <messageframe/Structures.hpp>
#include <messageframe/Value.hpp>
#include <string>

using msgframe::HybridMessageMap;
using msgframe::ParameterValue;
using msgframe::FrameConfig;

// --------------------------------------------------------------------
// Group: MemoryAndLifecycle — Checking the management of non-trivial objects
// --------------------------------------------------------------------

TEST(MemoryAndLifecycle, ValueCopySemanticsDoesNotLeakAndIsIndependent) {
    HybridMessageMap map;
    {
        std::string long_string(100, 'a');
        ParameterValue v(long_string);
        map.add("device_01", "payload", v);
    }

    const auto* found = map.find("device_01", "payload");
    CHECK_NOT_NULL(found);
    if (found) {
        auto s = found->tryGetString();
        CHECK(s.has_value());
        if (s) {
            CHECK_EQ(s->size(), static_cast<size_t>(100));
            CHECK_EQ((*s)[0], 'a');
        }
    }
}

TEST(MemoryAndLifecycle, ValueMoveSemanticsLeavesValidMovedFromState) {
    HybridMessageMap map;
    std::string long_string(128, 'b');
    ParameterValue original(long_string);

    map.add("device_01", "payload", std::move(original));

    // After std::move the original object should reset to Unknown and not hold the string
    auto s_orig = original.tryGetString();
    CHECK_FALSE(s_orig.has_value());

    // We check that the data on the map is preserved without damage.
    const auto* found = map.find("device_01", "payload");
    CHECK_NOT_NULL(found);
    if (found) {
        auto s_found = found->tryGetString();
        CHECK(s_found.has_value());
        if (s_found) CHECK_EQ(s_found->size(), static_cast<size_t>(128));
    }
}

TEST(MemoryAndLifecycle, ClearForcesImmediateHeapReleaseInMapMode) {
    HybridMessageMap map;
    // We fill the container above the limit to force the heap to be allocated under the map
    const size_t count = HybridMessageMap::SMALL_CAPACITY + 20;
    for (size_t i = 0; i < count; ++i) {
        map.add("dev", "param_" + std::to_string(i), ParameterValue(std::string("data")));
    }

    CHECK_EQ(map.size(), count);

    // Call clear. Thanks to our refactoring map_storage.reset()
    // the memory from the map should be returned to the system immediately.
    map.clear();
    CHECK_EQ(map.size(), static_cast<size_t>(0));

    // We check that the mode has reset to vector and is ready to accept data again
    map.add("dev", "new_param", ParameterValue(static_cast<int64_t>(42)));
    CHECK_EQ(map.size(), static_cast<size_t>(1));
}

// --------------------------------------------------------------------
// Group: StressConversion — Complex Vector -> Map transition scenarios
// --------------------------------------------------------------------

TEST(StressConversion, SeamlessFallbackWithDuplicateKeyPrevention) {
    HybridMessageMap map;

    // Insert 128 elements (vector mode on the boundary)
    for (size_t i = 0; i < HybridMessageMap::SMALL_CAPACITY; ++i) {
        map.add("dev", "p" + std::to_string(i), ParameterValue(static_cast<int64_t>(i)));
    }

    // We upsert (set) the existing element BEFORE converting
    map.set("dev", "p5", ParameterValue(static_cast<int64_t>(999)));
    CHECK_EQ(map.size(), HybridMessageMap::SMALL_CAPACITY); // Size has not changed, no duplicate

    // 129th element: provoke convert_to_map()
    map.add("dev", "trigger_conversion", ParameterValue(true));
    CHECK_EQ(map.size(), HybridMessageMap::SMALL_CAPACITY + 1);

    // We check that the updated element has been moved to the map with its new value.
    const auto* p5_found = map.find("dev", "p5");
    CHECK_NOT_NULL(p5_found);
    if (p5_found) {
        auto v = p5_found->tryGetInt();
        CHECK(v.has_value());
        if (v) CHECK_EQ(*v, static_cast<int64_t>(999));
    }
}

TEST(StressConversion, HeterogeneousLookupIsImmuneToTransientStrings) {
    HybridMessageMap map;
    // Force the transition to map mode
    const size_t count = HybridMessageMap::SMALL_CAPACITY + 5;
    for (size_t i = 0; i < count; ++i) {
        map.add("device_node", "metric_" + std::to_string(i), ParameterValue(static_cast<double>(i)));
    }

    // We test the resistance of transparent search to temporary objects (dangling references trap)
    const auto* found = map.find("device_node", std::string("metric_") + std::to_string(count - 1));
    CHECK_NOT_NULL(found);
}

// --------------------------------------------------------------------
// Group: Mutations — Checking inplace modifications and type contracts
// --------------------------------------------------------------------

TEST(Mutations, InplaceValueTypeMutationViaSet) {
    HybridMessageMap map;
    map.add("dev", "param", ParameterValue(int64_t(100)));

    // Changing the type of a value on the same key (intensive memory reuse)
    map.set("dev", "param", ParameterValue(std::string("mutated_to_string")));

    const auto* found = map.find("dev", "param");
    CHECK_NOT_NULL(found);
    if (found) {
        auto s = found->tryGetString();
        CHECK(s.has_value());
        if (s) CHECK_EQ(*s, std::string("mutated_to_string"));
    }
}

// --------------------------------------------------------------------
// Group: FrameConfigHints — Indirect verification of storage-mode
// selection via FrameConfig::initial_reserve.
//
// is_vector_mode is private, so we can't assert on it directly. Instead
// we exploit an observable side effect: HybridMessageMap::iterate()
// preserves insertion order in vector mode (flat contiguous storage)
// but does NOT in map mode (tsl::robin_map iterates in bucket order).
// With enough distinct keys, a hash-ordered iteration coinciding with
// insertion order by chance is statistically negligible.
// --------------------------------------------------------------------

namespace {
    void collect_flat_keys(std::string_view flat_key, const ParameterValue& /*val*/, void* user_data) {
        auto* keys = static_cast<std::vector<std::string>*>(user_data);
        keys->emplace_back(flat_key);
    }
} // namespace

TEST(FrameConfigHints, DefaultConfigPreservesInsertionOrderBelowThreshold) {
    // Control test: confirms the detection method itself is valid before
    // we rely on it for the map-mode assertions below.
    HybridMessageMap map; // FrameConfig{} — today's default behavior

    std::vector<std::string> expected_order;
    for (size_t i = 0; i < 10; ++i) {
        std::string device = "dev";
        std::string param = "p" + std::to_string(i);
        map.add(device, param, ParameterValue(static_cast<int64_t>(i)));
        expected_order.push_back(device + "\x1F" + param);
    }

    std::vector<std::string> observed_order;
    map.iterate(collect_flat_keys, &observed_order);

    CHECK_EQ(observed_order.size(), expected_order.size());
    CHECK(observed_order == expected_order);
}

TEST(FrameConfigHints, LargeInitialReserveStartsInMapModeImmediately) {
    FrameConfig cfg;
    cfg.initial_reserve = HybridMessageMap::SMALL_CAPACITY * 4; // e.g. 512

    HybridMessageMap map(cfg);

    // Insert well UNDER SMALL_CAPACITY. Without the hint this would stay
    // in vector mode; with the hint it must already be in map mode.
    std::vector<std::string> expected_order;
    const size_t count = 40;
    for (size_t i = 0; i < count; ++i) {
        std::string device = "dev";
        std::string param = "p" + std::to_string(i);
        map.add(device, param, ParameterValue(static_cast<int64_t>(i)));
        expected_order.push_back(device + "\x1F" + param);
    }

    CHECK_EQ(map.size(), count);

    std::vector<std::string> observed_order;
    map.iterate(collect_flat_keys, &observed_order);

    CHECK_EQ(observed_order.size(), expected_order.size());
    // Map mode => iteration order must NOT match insertion order.
    CHECK_FALSE(observed_order == expected_order);

    // Correctness, not just "it's a map": every key must still resolve.
    for (size_t i = 0; i < count; ++i) {
        const auto* v = map.find("dev", "p" + std::to_string(i));
        CHECK_NOT_NULL(v);
    }
}

TEST(FrameConfigHints, HintSurvivesClear) {
    // This is the regression test for the clear() fix — before it,
    // clear() unconditionally reset the container to vector mode and
    // forgot the original hint, defeating FrameConfig for any
    // reused-in-a-loop MessageFrame.
    FrameConfig cfg;
    cfg.initial_reserve = HybridMessageMap::SMALL_CAPACITY * 4;

    HybridMessageMap map(cfg);
    for (size_t i = 0; i < 5; ++i) {
        map.add("dev", "p" + std::to_string(i), ParameterValue(static_cast<int64_t>(i)));
    }

    map.clear();
    CHECK_EQ(map.size(), static_cast<size_t>(0));

    // Refill with only a handful of items — far below SMALL_CAPACITY.
    std::vector<std::string> expected_order;
    const size_t count = 40; // enough to make order-collision negligible
    for (size_t i = 0; i < count; ++i) {
        std::string device = "dev";
        std::string param = "q" + std::to_string(i);
        map.add(device, param, ParameterValue(static_cast<int64_t>(i)));
        expected_order.push_back(device + "\x1F" + param);
    }

    std::vector<std::string> observed_order;
    map.iterate(collect_flat_keys, &observed_order);

    // Still in map mode after clear() => hint was preserved.
    CHECK_FALSE(observed_order == expected_order);
}

TEST(FrameConfigHints, ZeroReserveClearStillResetsToVectorMode) {
    // Backward-compat guard: with NO hint (default FrameConfig, the same
    // as pre-patch behavior), clear() must still reset to vector mode —
    // we must not have silently changed default behavior.
    HybridMessageMap map; // initial_reserve == 0

    const size_t count = HybridMessageMap::SMALL_CAPACITY + 20;
    for (size_t i = 0; i < count; ++i) {
        map.add("dev", "p" + std::to_string(i), ParameterValue(static_cast<int64_t>(i)));
    }
    map.clear();

    std::vector<std::string> expected_order;
    for (size_t i = 0; i < 10; ++i) {
        std::string device = "dev";
        std::string param = "r" + std::to_string(i);
        map.add(device, param, ParameterValue(static_cast<int64_t>(i)));
        expected_order.push_back(device + "\x1F" + param);
    }

    std::vector<std::string> observed_order;
    map.iterate(collect_flat_keys, &observed_order);
    CHECK(observed_order == expected_order); // vector mode => order preserved
}

int main() {
    return msgframe_test::run_all();
}
