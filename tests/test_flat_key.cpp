#include "test_framework.hpp"
#include "messageframe/HybridMessageMap.hpp"
#include "messageframe/MessageFrame.hpp"
#include "messageframe/Structures.hpp"
#include "messageframe/Value.hpp"

// Тестуємо базову логіку створення та валідності FlatKey
TEST(FlatKeyValidation, ComposeAndBasicProperty) {
    auto key = msgframe::FlatKey::compose("sensor_alpha", "voltage");

    msgframe::HybridMessageMap map;
    msgframe::ParameterValue val = msgframe::VALUE(42);

    // Викликаємо метод (він повертає void / не-bool)
    map.add_flat(key, val);

    // Результат перевіряємо через пошук find()
    auto* found_val = map.find("sensor_alpha", "voltage");
    CHECK_NOT_NULL(found_val);
    if (found_val) {
        auto opt_res = found_val->tryGetInt();
        CHECK(opt_res.has_value());
        if (opt_res.has_value()) {
            CHECK_EQ(opt_res.value(), 42);
        }
    }
}

// Тестуємо життєвий цикл та збереження константи на рівні MessageFrame
TEST(FlatKeyValidation, LifecycleAndStorageAsConstant) {
    msgframe::MessageFrame frame;
    msgframe::ParameterValue val = msgframe::VALUE(12.34);

    const msgframe::FlatKey saved_key = msgframe::FlatKey::compose("device_5kw", "frequency");

    // Викликаємо методи без макросу CHECK
    frame.add_flat(saved_key, val);

    val = msgframe::VALUE(56.78);
    frame.set_flat(saved_key, val);

    // Перевіряємо успішність операцій через find_flat()
    auto* found_val = frame.find_flat(saved_key);
    CHECK_NOT_NULL(found_val);
    if (found_val) {
        auto opt_res = found_val->tryGetDouble();
        CHECK(opt_res.has_value());
        if (opt_res.has_value()) {
            CHECK_EQ(opt_res.value(), 56.78);
        }
    }
}

// Тестуємо стійкість інтерфейсу MessageFrame до flat-методів
TEST(FlatKeyValidation, MessageFrameIntegration) {
    msgframe::MessageFrame frame;
    msgframe::ParameterValue val = msgframe::VALUE("active");

    auto key = msgframe::FlatKey::compose("system", "status");

    // Викликаємо методи модифікації
    frame.add_flat(key, val);

    val = msgframe::VALUE("maintenance");
    frame.update_flat(key, val);

    // Перевіряємо через стандартний двокомпонентний find()
    auto* found_val = frame.find("system", "status");
    CHECK_NOT_NULL(found_val);
    if (found_val) {
        auto opt_res = found_val->tryGetString();
        CHECK(opt_res.has_value());
        if (opt_res.has_value()) {
            CHECK_EQ(opt_res.value(), "maintenance");
        }
    }
}

// Тестуємо семантику копіювання та переміщення для FlatKey
TEST(FlatKeyValidation, CopyAndMoveSemantics) {
    auto original_key = msgframe::FlatKey::compose("sensor_beta", "current");

    // Конструктор копіювання
    msgframe::FlatKey copied_key(original_key);

    msgframe::HybridMessageMap map;
    msgframe::ParameterValue val = msgframe::VALUE(15.5);

    map.add_flat(copied_key, val);
    CHECK_NOT_NULL(map.find_flat(original_key));

    // Оператор присвоювання копіюванням
    auto assigned_key = msgframe::FlatKey::compose("temp", "dummy");
    assigned_key = original_key;
    CHECK_NOT_NULL(map.find_flat(assigned_key));

    // Конструктор переміщення
    msgframe::FlatKey moved_key(std::move(original_key));

    auto* found_val = map.find_flat(moved_key);
    CHECK_NOT_NULL(found_val);
    if (found_val) {
        auto opt_res = found_val->tryGetDouble();
        CHECK(opt_res.has_value());
        if (opt_res.has_value()) {
            CHECK_EQ(opt_res.value(), 15.5);
        }
    }

    // Оператор присвоювання переміщенням
    msgframe::FlatKey another_moved_key = msgframe::FlatKey::compose("temp", "dummy2");
    another_moved_key = std::move(moved_key);
    CHECK_NOT_NULL(map.find_flat(another_moved_key));
}

int main() {
    return msgframe_test::run_all();
}
