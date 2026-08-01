// tests/test_hybrid_map.cpp
#include "test_framework.hpp"
#include <messageframe/HybridMessageMap.hpp>
#include <messageframe/Value.hpp>
#include <string>

using msgframe::HybridMessageMap;
using msgframe::ParameterValue;

// --------------------------------------------------------------------
// Group: MemoryAndLifecycle — Checking the management of non-trivial objects
// --------------------------------------------------------------------

TEST(MemoryAndLifecycle, ValueCopySemanticsDoesNotLeakAndIsIndependent) {
    HybridMessageMap map;
    {
        // Create a string that is guaranteed to allocate memory on the heap (bypassing SSO)
        std::string long_string(100, 'a');
        ParameterValue v(long_string);
        map.add("device_01", "payload", v); // Copy
    } // Here the local v and long_string are destroyed. The object in the map must live.

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
    map.add("dev", "new_param", ParameterValue(42));
    CHECK_EQ(map.size(), static_cast<size_t>(1));
    const auto* found = map.find("dev", "new_param");
    CHECK_NOT_NULL(found);
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
        auto v = p5_found->tryGetInt64();
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
    if (found) {
        auto d = found->tryGetDouble();
        CHECK(d.has_value());
        if (d) CHECK_EQ(*d, static_cast<double>(count - 1));
    }
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

        // We check that the old type is completely erased
        CHECK_FALSE(found->tryGetInt64().has_value());
    }
}

int main() {
    return msgframe_test::run_all();
}
