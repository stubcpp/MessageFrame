// examples/basic_usage.cpp
//
// Мінімальний приклад використання бібліотеки MessageFrame.
// Мета: показати базовий API за 30 секунд читання — без бенчмарків,
// без замірів часу, без CLI-аргументів. Якщо хочеш побачити продуктивність,
// дивись benchmarks/benchmark.cpp.

#include <messageframe/MessageFrame.hpp>
#include <iostream>

// Колбек для iterate_parameters() — викликається для кожного параметра
// в повідомленні, без створення тимчасових std::string для ключа.
void printParam(std::string_view flat_key, const msgframe::ParameterValue& val, void* /*user_data*/) {
    std::cout << "  " << flat_key << " = " << val.toString() << "\n";
}

int main() {
    // 1. Створення повідомлення.
    //    Конструктор: (message_id, message_type, flags, version, sequence_number)
    msgframe::MessageFrame msg(/*id=*/1001, /*type=*/1, /*flags=*/0, /*version=*/1, /*seq=*/1);

    // 2. Додавання параметрів через add("device", "parameter", VALUE(...)).
    //    VALUE() — фабрика, що приймає double/bool/int/string і повертає ParameterValue.
    msg.add("sensor_alpha", "voltage", msgframe::VALUE(12.6));
    msg.add("sensor_alpha", "status_ok", msgframe::VALUE(true));
    msg.add("device_core", "fw_version", msgframe::VALUE("v3.2.1"));

    std::cout << "Created message with " << msg.parameters_size() << " parameters.\n\n";

    // 3. Пошук конкретного параметра без алокацій (Zero-Allocation find).
    std::cout << "Looking up sensor_alpha.voltage:\n";
    if (const auto* value = msg.find("sensor_alpha", "voltage")) {
        std::cout << "  found: " << value->toString() << "\n\n";
    } else {
        std::cout << "  not found\n\n";
    }

    // 4. Обхід усіх параметрів повідомлення через колбек.
    std::cout << "All parameters:\n";
    msg.iterate_parameters(printParam, nullptr);

    return 0;
}
