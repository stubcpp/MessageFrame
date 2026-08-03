#include <messageframe/MessageFrame.hpp>
#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include <iomanip>
#include <cstdlib>
#include <cstring>

#ifdef _MSC_VER
    #include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
    #include <cpuid.h>
#else
    #error "Unsupported compiler for high-resolution timing"
#endif

// Enum for beautiful testing of strong header typing
enum class MyMsgId : int32_t {
    TELEMETRY_PACKET = 1001,
    COMMAND_PACKET = 1002
};

enum class MyMsgType : int32_t {
    PERIODIC = 1,
    CRITICAL = 2
};

// A simple callback to demonstrate fast iterator traversal
void printParamCallback(std::string_view flat_key, const msgframe::ParameterValue& val, void* /*user_data*/) {
    size_t sep_pos = flat_key.find('\x1F');
    std::cout << "  [Iterate] ";
    if (sep_pos != std::string_view::npos) {
        std::cout << flat_key.substr(0, sep_pos) << "." << flat_key.substr(sep_pos + 1);
    } else {
        std::cout << flat_key;
    }
    std::cout << " = " << val.toString() << "\n";
}


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

// Universal wrapper for CPUID
void native_cpuid(int cpu_info[4], unsigned int function_id) {
#ifdef _MSC_VER
    __cpuid(cpu_info, function_id);
#elif defined(__GNUC__) || defined(__clang__)
    // GCC/Clang require passing references to individual variables
    __get_cpuid(function_id,
                (unsigned int*)&cpu_info[0],
                (unsigned int*)&cpu_info[1],
                (unsigned int*)&cpu_info[2],
                (unsigned int*)&cpu_info[3]);
#endif
}

// Get the cpu model name
std::string get_cpu_model_name() {
    // Array for storing the results of the cpuid instruction (registers EAX, EBX, ECX, EDX)
    int cpu_info[4] = { 0 };

    // Checking if the processor supports advanced cpuid functions
    native_cpuid(cpu_info, 0x80000000);
    unsigned int nExIds = cpu_info[0];

    if (nExIds < 0x80000004) {
        return "Unknown Processor (CPUID not supported)";
    }

    char cpu_brand_string[49] = { 0 };

    // The model name is assembled sequentially from three functions: 0x80000002, 0x80000003, 0x80000004
    for (unsigned int i = 0x80000002; i <= 0x80000004; ++i) {
        native_cpuid(cpu_info, i);

        // Copy 16 bytes from registers EAX, EBX, ECX, EDX
        std::memcpy(cpu_brand_string + (i - 0x80000002) * 16, cpu_info, sizeof(cpu_info));
    }

    // Cleaning up possible leading spaces that Intel sometimes returns
    std::string model_name(cpu_brand_string);
    size_t first_letter = model_name.find_first_not_of(" ");
    if (first_letter != std::string::npos) {
        model_name = model_name.substr(first_letter);
    }

    return model_name;
}

// ----------------------------------------------------------------
// Move the benchmark itself into a separate function.
// ----------------------------------------------------------------
void runBenchmark(const BenchmarkConfig& cfg) {
    // STEP 1: The key pool is generated ONCE before the measurement starts,
    // regardless of PARAMS_COUNT — it scales together with cfg.params_count.
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
    double sum_find{};

    // STEP 4 setup: benchmark find() against the LAST-inserted param on
    // purpose. In vector mode find() is a linear std::find_if scan from the
    // front, so the last-inserted key is the worst case; in map mode
    // position doesn't matter (O(1) either way). This keeps the number
    // honest for both modes instead of flattering vector mode with an
    // early/near-front lookup.
    std::string_view last_param_name = key_pool[cfg.params_count - 1];

    for (size_t i = 0; i < cfg.iterations; ++i) {
        serialization_buffer.clear();

        msgframe::MessageFrame bench_msg(200, 7, 1, 2, i);

        auto t0 = std::chrono::high_resolution_clock::now();
        for (size_t p = 0; p < cfg.params_count; ++p) {
            // STEP 2: The parameter value depends ONLY on p, not on i.
            // This keeps the size of each message (and therefore msgpack
            // varint encoding) constant regardless of ITERATIONS —
            // otherwise "Avg Packed Size" and its derived metrics float
            // between runs with different iteration numbers (the value of p+i grows
            // with i, and msgpack encodes large integers with longer varint sequences:
            // 0-127 -> 1 byte, 128-65535 -> 2-3 bytes, 65536+ -> 5 bytes).
            std::string_view static_param_name = key_pool[p];
            bench_msg.add("dev", static_param_name, msgframe::VALUE(static_cast<int64_t>(p)));
        }
        auto t1 = std::chrono::high_resolution_clock::now();

        if (i == 0) {
            std::cout << "[Sanity Check] Message 0 populated. Total params in map: "
                << bench_msg.parameters_size() << "\n\n";
        }

        // STEP 4: Zero-allocation lookup, worst-case position (see setup above).
        // volatile prevents the optimizer from hoisting/eliding the call since
        // the result would otherwise look unused from the compiler's point of view.
        volatile const void* found_ptr = bench_msg.find("dev", last_param_name);
        (void)found_ptr;
        auto t1_5 = std::chrono::high_resolution_clock::now();
        
        bench_msg.serialize(serialization_buffer);
        auto t2 = std::chrono::high_resolution_clock::now();
        total_bytes_processed += serialization_buffer.size();

        msgframe::MessageFrame receiver_msg;
        if (receiver_msg.deserialize(serialization_buffer.data(), serialization_buffer.size())) {
            successful_deserializations++;
        }
        auto t3 = std::chrono::high_resolution_clock::now();

        sum_add += std::chrono::duration<double, std::micro>(t1 - t0).count();
        sum_find += std::chrono::duration<double, std::micro>(t1_5 - t1).count();
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
    std::cout << "sum_add:           " << (sum_add / cfg.iterations) << " us\n";
    std::cout << "sum_find:          " << (sum_find / cfg.iterations) << " us  (worst-case: last-inserted key)\n";
    std::cout << "sum_serialize:     " << (sum_serialize / cfg.iterations) << " us\n";
    std::cout << "sum_deserialize:   " << (sum_deserialize / cfg.iterations) << " us\n";
    std::cout << "==================================================\n";
}

int main(int argc, char** argv) {
    BenchmarkConfig cfg = parseArgs(argc, argv);

    std::cout << "==================================================\n";
    std::cout << "   LOW-LATENCY MESSAGE FRAME LIBRARY BENCHMARK    \n";
    std::cout << "==================================================\n\n";

    // ----------------------------------------------------------------
    // PART 1: API DEMONSTRATION
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
    // PART 2: HIGH-PRECISION PERFORMANCE BENCHMARK
    // Now a single pass handles both "small" (vector mode, < 128
    // parameters) and "large" (map mode, > 128 parameters) configurations —
    // just pass --params 4 or --params 150 from the command line.
    // ----------------------------------------------------------------
    std::cout << "--- Step 3: Performance Benchmark ---\n";
    std::cout << "CPU: " << get_cpu_model_name() << '\n';
    runBenchmark(cfg);

    return 0;
}
