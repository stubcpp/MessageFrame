// tests/test_hybrid_map.cpp
//
// Юніт-тести коректності HybridMessageMap. Покривають саме ті місця,
// де вже траплялись реальні баги під час розробки:
//   - update_flat(ParameterValue&&) без return (UB на rvalue-перевантаженні)
//   - межовий випадок конвертації vector -> map на SMALL_CAPACITY-му елементі
//   - heterogeneous lookup через std::string_view без зайвої std::string-алокації

#include "test_framework.hpp"
#include <messageframe/HybridMessageMap.hpp>
#include <messageframe/Value.hpp>
#include <string>

using msgframe::HybridMessageMap;
using msgframe::ParameterValue;

// --------------------------------------------------------------------
// Group: AddFind — базовий round-trip у vector-режимі (< SMALL_CAPACITY)
// --------------------------------------------------------------------

TEST(AddFind, LvalueAddIsFoundByDeviceParam) {
    HybridMessageMap map;
    ParameterValue v(12.6);

    map.add("sensor_alpha", "voltage", v);

    const auto* found = map.find("sensor_alpha", "voltage");
    CHECK_NOT_NULL(found);
    if (found) {
        auto d = found->tryGetDouble();
        CHECK(d.has_value());
        if (d) CHECK_EQ(*d, 12.6);
    }
}

TEST(AddFind, RvalueAddIsFoundByFlatKey) {
    HybridMessageMap map;
    map.add_flat("device_core.fw_version", ParameterValue(1.0));

    const auto* found = map.find_flat("device_core.fw_version");
    CHECK_NOT_NULL(found);
}

TEST(AddFind, MissingKeyReturnsNull) {
    HybridMessageMap map;
    map.add("sensor_alpha", "voltage", ParameterValue(1.0));

    CHECK_NULL(map.find("sensor_alpha", "does_not_exist"));
    CHECK_NULL(map.find("ghost_device", "voltage"));
}

TEST(AddFind, SizeReflectsInsertedCount) {
    HybridMessageMap map;
    CHECK_EQ(map.size(), static_cast<size_t>(0));

    map.add("dev", "p1", ParameterValue(1.0));
    map.add("dev", "p2", ParameterValue(2.0));
    CHECK_EQ(map.size(), static_cast<size_t>(2));
}

TEST(AddFind, ClearEmptiesContainerAndResetsToVectorMode) {
    HybridMessageMap map;
    map.add("dev", "p1", ParameterValue(1.0));
    map.clear();

    CHECK_EQ(map.size(), static_cast<size_t>(0));
    CHECK_NULL(map.find("dev", "p1"));

    // Після clear() контейнер має знову приймати елементи нормально
    map.add("dev", "p1", ParameterValue(5.0));
    CHECK_EQ(map.size(), static_cast<size_t>(1));
}

// --------------------------------------------------------------------
// Group: MapConversion — межовий випадок vector -> map на SMALL_CAPACITY
// --------------------------------------------------------------------

TEST(MapConversion, StaysInVectorModeBelowCapacity) {
    HybridMessageMap map;
    for (size_t i = 0; i < HybridMessageMap::SMALL_CAPACITY; ++i) {
        map.add("dev", "p" + std::to_string(i), ParameterValue(static_cast<double>(i)));
    }
    CHECK_EQ(map.size(), HybridMessageMap::SMALL_CAPACITY);

    // Усі SMALL_CAPACITY елементів мають бути знайдені до конвертації
    for (size_t i = 0; i < HybridMessageMap::SMALL_CAPACITY; ++i) {
        std::string param = "p" + std::to_string(i);
        const auto* found = map.find("dev", param);
        CHECK_NOT_NULL(found);
    }
}

TEST(MapConversion, ConvertsToMapModeOnceCapacityExceeded) {
    HybridMessageMap map;
    // SMALL_CAPACITY + кілька елементів -> примусово переходимо в map-режим
    const size_t count = HybridMessageMap::SMALL_CAPACITY + 10;
    for (size_t i = 0; i < count; ++i) {
        map.add("dev", "p" + std::to_string(i), ParameterValue(static_cast<double>(i)));
    }

    CHECK_EQ(map.size(), count);

    // Перевіряємо елементи з обох боків межі конвертації:
    // ті, що були додані ДО переходу в map-режим, і ПІСЛЯ.
    const auto* before_boundary = map.find("dev", "p0");
    const auto* at_boundary = map.find("dev", "p" + std::to_string(HybridMessageMap::SMALL_CAPACITY - 1));
    const auto* after_boundary = map.find("dev", "p" + std::to_string(HybridMessageMap::SMALL_CAPACITY));
    const auto* last = map.find("dev", "p" + std::to_string(count - 1));

    CHECK_NOT_NULL(before_boundary);
    CHECK_NOT_NULL(at_boundary);
    CHECK_NOT_NULL(after_boundary);
    CHECK_NOT_NULL(last);
}

TEST(MapConversion, FindFlatWorksAfterConversionViaStringView) {
    // Регресійний тест саме на heterogeneous lookup: переконуємось, що
    // find_flat зі string_view знаходить елемент у map-режимі без
    // потреби явно конструювати std::string на боці викликаючого коду.
    HybridMessageMap map;
    const size_t count = HybridMessageMap::SMALL_CAPACITY + 5;
    for (size_t i = 0; i < count; ++i) {
        map.add_flat("dev." + std::to_string(i), ParameterValue(static_cast<double>(i)));
    }

    std::string owned_key = "dev." + std::to_string(count - 1);
    std::string_view view_key = owned_key; // чистий view, без володіння

    const auto* found = map.find_flat(view_key);
    CHECK_NOT_NULL(found);
    if (found) {
        auto d = found->tryGetDouble();
        CHECK(d.has_value());
        if (d) CHECK_EQ(*d, static_cast<double>(count - 1));
    }
}

// --------------------------------------------------------------------
// Group: UpdateSemantics — strict update(): саме тут раніше губився
// return у rvalue-перевантаженні update_flat(ParameterValue&&)
// --------------------------------------------------------------------

TEST(UpdateSemantics, UpdateExistingKeyLvalueReturnsTrueAndChangesValue) {
    HybridMessageMap map;
    map.add("dev", "voltage", ParameterValue(1.0));

    ParameterValue new_val(99.0);
    bool ok = map.update("dev", "voltage", new_val);

    CHECK(ok);
    const auto* found = map.find("dev", "voltage");
    CHECK_NOT_NULL(found);
    if (found) {
        auto d = found->tryGetDouble();
        CHECK(d.has_value());
        if (d) CHECK_EQ(*d, 99.0);
    }
}

TEST(UpdateSemantics, UpdateExistingKeyRvalueReturnsTrueAndChangesValue) {
    // Це саме той випадок, де колись був забутий `return` у
    // update_flat(std::string_view, ParameterValue&&) — функція
    // повертала непередбачуване значення (UB) замість true.
    HybridMessageMap map;
    map.add("dev", "voltage", ParameterValue(1.0));

    bool ok = map.update("dev", "voltage", ParameterValue(42.0));

    CHECK(ok);
    const auto* found = map.find("dev", "voltage");
    CHECK_NOT_NULL(found);
    if (found) {
        auto d = found->tryGetDouble();
        CHECK(d.has_value());
        if (d) CHECK_EQ(*d, 42.0);
    }
}

TEST(UpdateSemantics, UpdateMissingKeyLvalueReturnsFalseAndDoesNotInsert) {
    HybridMessageMap map;
    ParameterValue v(1.0);

    bool ok = map.update("dev", "does_not_exist", v);

    CHECK_FALSE(ok);
    CHECK_EQ(map.size(), static_cast<size_t>(0));
    CHECK_NULL(map.find("dev", "does_not_exist"));
}

TEST(UpdateSemantics, UpdateMissingKeyRvalueReturnsFalseAndDoesNotInsert) {
    HybridMessageMap map;

    bool ok = map.update("dev", "does_not_exist", ParameterValue(1.0));

    CHECK_FALSE(ok);
    CHECK_EQ(map.size(), static_cast<size_t>(0));
}

TEST(UpdateSemantics, UpdateRvalueInMapModeReturnsTrueForExistingKey) {
    // Той самий rvalue-update-без-return баг, але вже у map-режимі
    // (гілка update_flat_impl, що йде через map_storage->map.find,
    // а не через vector_storage).
    HybridMessageMap map;
    const size_t count = HybridMessageMap::SMALL_CAPACITY + 5;
    for (size_t i = 0; i < count; ++i) {
        map.add("dev", "p" + std::to_string(i), ParameterValue(static_cast<double>(i)));
    }

    bool ok = map.update("dev", "p3", ParameterValue(777.0));
    CHECK(ok);

    const auto* found = map.find("dev", "p3");
    CHECK_NOT_NULL(found);
    if (found) {
        auto d = found->tryGetDouble();
        CHECK(d.has_value());
        if (d) CHECK_EQ(*d, 777.0);
    }

    bool missing_ok = map.update("dev", "ghost_param", ParameterValue(1.0));
    CHECK_FALSE(missing_ok);
}

// --------------------------------------------------------------------
// Group: SetSemantics — upsert: оновлює існуючий або додає новий
// --------------------------------------------------------------------

TEST(SetSemantics, SetOnMissingKeyInsertsNewEntry) {
    HybridMessageMap map;
    map.set("dev", "voltage", ParameterValue(5.0));

    CHECK_EQ(map.size(), static_cast<size_t>(1));
    const auto* found = map.find("dev", "voltage");
    CHECK_NOT_NULL(found);
}

TEST(SetSemantics, SetOnExistingKeyOverwritesWithoutDuplicating) {
    HybridMessageMap map;
    map.add("dev", "voltage", ParameterValue(5.0));
    map.set("dev", "voltage", ParameterValue(9.0));

    // Не повинно з'явитись дублікату — size лишається 1
    CHECK_EQ(map.size(), static_cast<size_t>(1));
    const auto* found = map.find("dev", "voltage");
    CHECK_NOT_NULL(found);
    if (found) {
        auto d = found->tryGetDouble();
        CHECK(d.has_value());
        if (d) CHECK_EQ(*d, 9.0);
    }
}

// --------------------------------------------------------------------
// Group: Iterate — обхід контейнера в обох режимах
// --------------------------------------------------------------------

namespace {
struct IterateCounter {
    size_t count = 0;
    double sum = 0.0;
};

void count_and_sum(std::string_view /*flat_key*/, const ParameterValue& val, void* user_data) {
    auto* counter = static_cast<IterateCounter*>(user_data);
    counter->count++;
    if (auto d = val.tryGetDouble()) {
        counter->sum += *d;
    }
}
} // namespace

TEST(Iterate, VisitsEveryElementInVectorMode) {
    HybridMessageMap map;
    map.add("dev", "p1", ParameterValue(1.0));
    map.add("dev", "p2", ParameterValue(2.0));
    map.add("dev", "p3", ParameterValue(3.0));

    IterateCounter counter;
    map.iterate(count_and_sum, &counter);

    CHECK_EQ(counter.count, static_cast<size_t>(3));
    CHECK_EQ(counter.sum, 6.0);
}

TEST(Iterate, VisitsEveryElementInMapMode) {
    HybridMessageMap map;
    const size_t count = HybridMessageMap::SMALL_CAPACITY + 7;
    double expected_sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        map.add("dev", "p" + std::to_string(i), ParameterValue(static_cast<double>(i)));
        expected_sum += static_cast<double>(i);
    }

    IterateCounter counter;
    map.iterate(count_and_sum, &counter);

    CHECK_EQ(counter.count, count);
    CHECK_EQ(counter.sum, expected_sum);
}

int main() {
    return msgframe_test::run_all();
}
