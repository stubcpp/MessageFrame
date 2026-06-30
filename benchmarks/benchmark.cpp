#include <messageframe/MessageFrame.hpp>
#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <intrin.h>

// Перерахування для красивого тестування строгої типізації хедера
enum class MyMsgId : int32_t {
    TELEMETRY_PACKET = 1001,
    COMMAND_PACKET = 1002
};

enum class MyMsgType : int32_t {
    PERIODIC = 1,
    CRITICAL = 2
};

// Простий колбек для демонстрації швидкого обходу ітератором
void printParamCallback(std::string_view flat_key, const msgframe::ParameterValue& val, void* /*user_data*/) {
    std::cout << "  [Iterate] " << flat_key << " = " << val.toString() << "\n";
}

// ----------------------------------------------------------------
// Параметри запуску бенчмарку, винесені з тіла main() так, щоб
// ITERATIONS і PARAMS_COUNT можна було задавати без перекомпіляції
// і без закомментовування шматків коду.
// ----------------------------------------------------------------
struct BenchmarkConfig {
    size_t iterations = 100'000;
    size_t params_count = 150;
};

void printUsage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [--iterations N] [--params N]\n"
              << "  --iterations N   Number of message lifecycle iterations (default: 100000)\n"
              << "  --params N       Number of parameters per message (default: 150)\n"
              << "  -h, --help       Show this help message\n";
}

BenchmarkConfig parseArgs(int argc, char** argv) {
    BenchmarkConfig cfg;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            cfg.iterations = static_cast<size_t>(std::strtoull(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--params") == 0 && i + 1 < argc) {
            cfg.params_count = static_cast<size_t>(std::strtoull(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            std::exit(0);
        } else {
            std::cerr << "Unknown argument: " << argv[i] << "\n";
            printUsage(argv[0]);
            std::exit(1);
        }
    }
    return cfg;
}

// Get the cpu model name
std::string get_cpu_model_name() {
    // Масив для збереження результатів інструкції cpuid (регістри EAX, EBX, ECX, EDX)
    int cpu_info[4] = { 0 };

    // Перевіряємо, чи підтримує процесор розширені функції cpuid
    __cpuid(cpu_info, 0x80000000);
    unsigned int nExIds = cpu_info[0];

    if (nExIds < 0x80000004) {
        return "Unknown Processor (CPUID not supported)";
    }

    char cpu_brand_string[49] = { 0 };

    // Назва моделі збирається послідовно з трьох функцій: 0x80000002, 0x80000003, 0x80000004
    for (unsigned int i = 0x80000002; i <= 0x80000004; ++i) {
        __cpuid(cpu_info, i);

        // Копіюємо 16 байт з регістрів EAX, EBX, ECX, EDX
        std::memcpy(cpu_brand_string + (i - 0x80000002) * 16, cpu_info, sizeof(cpu_info));
    }

    // Очищаємо можливі початкові пробіли, які іноді повертає Intel
    std::string model_name(cpu_brand_string);
    size_t first_letter = model_name.find_first_not_of(" ");
    if (first_letter != std::string::npos) {
        model_name = model_name.substr(first_letter);
    }

    return model_name;
}

// ----------------------------------------------------------------
// Виносимо сам бенчмарк у окрему функцію — раніше довелось би
// закоментовувати "стару" гілку з 4 параметрами і розкоментовувати
// "нову" з 150, щоб перевірити різні конфігурації. Тепер це один
// прохід, що приймає params_count/iterations як аргументи.
// ----------------------------------------------------------------
void runBenchmark(const BenchmarkConfig& cfg) {
    // КРОК 1: Пул ключів формується ОДИН раз перед стартом вимірювання,
    // незалежно від PARAMS_COUNT — масштабується разом з cfg.params_count.
    std::vector<std::string> key_pool;
    key_pool.reserve(cfg.params_count);
    for (size_t p = 0; p < cfg.params_count; ++p) {
        key_pool.push_back("param_" + std::to_string(p));
    }

    std::vector<uint8_t> serialization_buffer;
    serialization_buffer.reserve(32768);

    std::cout << "Running " << cfg.iterations << " iterations with "
              << cfg.params_count << " parameters each...\n";
    std::cout << "(Zero-Allocation key injection via pre-allocated Key Pool)\n\n";

    auto start_time = std::chrono::high_resolution_clock::now();

    size_t total_bytes_processed = 0;
    size_t successful_deserializations = 0;

    double sum_add{};
    double sum_serialize{};
    double sum_deserialize{};

    for (size_t i = 0; i < cfg.iterations; ++i) {
        msgframe::MessageFrame bench_msg(200, 7, 1, 2, i);

        auto t0 = std::chrono::high_resolution_clock::now();
        for (size_t p = 0; p < cfg.params_count; ++p) {
            // КРОК 2: Значення параметра залежить ТІЛЬКИ від p, а не від i.
            // Завдяки цьому розмір кожного повідомлення (і відповідно msgpack
            // varint-кодування) лишається сталим незалежно від ITERATIONS —
            // інакше "Avg Packed Size" і похідні від нього метрики плавають
            // між прогонами з різним числом ітерацій (значення p+i росте
            // разом з i, а msgpack кодує великі цілі довшими varint-послідовностями:
            // 0-127 -> 1 байт, 128-65535 -> 2-3 байти, 65536+ -> 5 байт).
            std::string_view static_param_name = key_pool[p];
            bench_msg.add("dev", static_param_name, msgframe::VALUE(static_cast<int64_t>(p)));
        }
        auto t1 = std::chrono::high_resolution_clock::now();

        if (i == 0) {
            std::cout << "[Sanity Check] Message 0 populated. Total params in map: "
                << bench_msg.parameters_size() << "\n\n";
        }

        bench_msg.serialize(serialization_buffer);
        auto t2 = std::chrono::high_resolution_clock::now();
        total_bytes_processed += serialization_buffer.size();

        msgframe::MessageFrame receiver_msg;
        if (receiver_msg.deserialize(serialization_buffer.data(), serialization_buffer.size())) {
            successful_deserializations++;
        }
        auto t3 = std::chrono::high_resolution_clock::now();

        sum_add += std::chrono::duration<double, std::micro>(t1 - t0).count();
        sum_serialize += std::chrono::duration<double, std::micro>(t2 - t1).count();
        sum_deserialize += std::chrono::duration<double, std::micro>(t3 - t2).count();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end_time - start_time;

    double avg_time_per_msg_us = (duration.count() * 1000.0) / cfg.iterations;
    double msg_per_second = (cfg.iterations / duration.count()) * 1000.0;
    double throughput_mb = (static_cast<double>(total_bytes_processed) / (1024.0 * 1024.0)) / (duration.count() / 1000.0);

    std::cout << "=================== RESULTS ======================\n";
    std::cout << "Iterations:        " << cfg.iterations << "\n";
    std::cout << "Params per msg:    " << cfg.params_count << "\n";
    std::cout << "Total Time:        " << std::fixed << std::setprecision(2) << duration.count() << " ms\n";
    std::cout << "Avg Time per Msg:  " << std::fixed << std::setprecision(3) << avg_time_per_msg_us << " microseconds (us)\n";
    std::cout << "Performance:       " << std::fixed << std::setprecision(0) << msg_per_second << " messages/sec\n";
    std::cout << "Throughput:        " << std::fixed << std::setprecision(2) << throughput_mb << " MB/sec\n";
    std::cout << "Success Rate:      " << (successful_deserializations == cfg.iterations ? "100% OK" : "ERROR") << "\n";
    std::cout << "Avg Packed Size:   " << (total_bytes_processed / cfg.iterations) << " bytes\n";
    std::cout << "sum_add:   " << (sum_add / cfg.iterations) << " us\n";
    std::cout << "sum_serialize:   " << (sum_serialize / cfg.iterations) << " us\n";
    std::cout << "sum_deserialize:   " << (sum_deserialize / cfg.iterations) << " us\n";
    std::cout << "==================================================\n";
}

int main(int argc, char** argv) {
    BenchmarkConfig cfg = parseArgs(argc, argv);

    std::cout << "==================================================\n";
    std::cout << "   LOW-LATENCY MESSAGE FRAME LIBRARY BENCHMARK    \n";
    std::cout << "==================================================\n\n";

    // ----------------------------------------------------------------
    // ЧАСТИНА 1: ДЕМОНСТРАЦІЯ РОБОТИ З API
    // ----------------------------------------------------------------
    std::cout << "--- Step 1: Creating and populating a message ---\n";

    msgframe::MessageFrame msg(MyMsgId::TELEMETRY_PACKET, MyMsgType::CRITICAL, 50, 99, 1);

    msg.header().setFlags(0xAA00);
    msg.header().setMessageId(MyMsgId::COMMAND_PACKET);
    msg.header().setMessageType(MyMsgType::PERIODIC);
    msg.header().updateTimestamp();

    msg.add("sensor_alpha", "voltage", msgframe::VALUE(12.6));
    msg.add("sensor_alpha", "status_ok", msgframe::VALUE(true));
    msg.add("device_core", "fw_version", msgframe::VALUE("v3.2.1"));
    msg.add("device_core", "error_codes", msgframe::VALUE(-5));

    std::vector<uint8_t> dummy_iq = { 0x01, 0x02, 0x03, 0x04, 0x05, 0xAA, 0xBB, 0xCC };
    msg.add_attachment("raw_iq_stream", std::move(dummy_iq));

    std::cout << "Message created successfully.\n";
    std::cout << "Header Timestamp: " << msg.header().getTimestamp() << " ms\n";
    std::cout << "Header MsgID:     " << msg.header().getMessageIdRaw() << "\n";
    std::cout << "Total parameters: " << msg.parameters_size() << "\n";
    std::cout << "Total attachments: " << msg.get_attachments().size() << "\n\n";

    std::cout << "--- Step 2: Testing Zero-Allocation Search ---\n";
    if (const auto* val = msg.find("sensor_alpha", "voltage")) {
        std::cout << "Found sensor_alpha.voltage: " << val->toString() << "\n";
    }

    msg.iterate_parameters(printParamCallback, nullptr);
    std::cout << "\n";

    // ----------------------------------------------------------------
    // ЧАСТИНА 2: ВИСОКОТОЧНИЙ БЕНЧМАРК ПРОДУКТИВНОСТІ
    // Тепер один прохід обслуговує і "маленькі" (vector-режим, < 128
    // параметрів), і "великі" (map-режим, > 128 параметрів) конфігурації —
    // достатньо передати --params 4 або --params 150 з командного рядка.
    // ----------------------------------------------------------------
    std::cout << "--- Step 3: Performance Benchmark ---\n";
    std::cout << "CPU: " << get_cpu_model_name() << '\n';
    runBenchmark(cfg);

    return 0;
}
