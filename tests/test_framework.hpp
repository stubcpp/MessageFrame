// tests/test_framework.hpp
//
// Мінімальний zero-dependency test harness. Без GoogleTest/Catch2 —
// у дусі бібліотеки: мінімум залежностей, швидка компіляція, простий вивід.
//
// Використання:
//   TEST(group_name, test_name) {
//       CHECK(condition);
//       CHECK_EQ(actual, expected);
//   }
//   ...
//   int main() { return msgframe_test::run_all(); }

#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <sstream>

namespace msgframe_test {

struct TestCase {
    std::string group;
    std::string name;
    std::function<void()> fn;
};

struct Failure {
    std::string message;
};

// Глобальний реєстр тестів. inline-функція, щоб уникнути ODR-проблем
// при включенні цього хедера в кілька .cpp файлів тестів.
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

// Поточний список провалів усередині тесту, що виконується.
// Один тест може мати кілька CHECK() — не зупиняємось на першому провалі,
// щоб одразу побачити всі проблеми в тесті за один прогін.
inline std::vector<std::string>& current_failures() {
    static std::vector<std::string> failures;
    return failures;
}

struct Registrar {
    Registrar(const std::string& group, const std::string& name, std::function<void()> fn) {
        registry().push_back({group, name, std::move(fn)});
    }
};

inline void record_failure(const std::string& expr, const std::string& file, int line) {
    std::ostringstream oss;
    oss << file << ":" << line << ": CHECK failed: " << expr;
    current_failures().push_back(oss.str());
}

template<typename A, typename B>
void record_failure_eq(const A& actual, const B& expected, const std::string& expr,
                        const std::string& file, int line) {
    std::ostringstream oss;
    oss << file << ":" << line << ": CHECK_EQ failed: " << expr
        << "  (actual=" << actual << ", expected=" << expected << ")";
    current_failures().push_back(oss.str());
}

inline int run_all() {
    int total = 0;
    int passed = 0;
    std::vector<std::string> failed_names;

    for (const auto& tc : registry()) {
        ++total;
        current_failures().clear();

        try {
            tc.fn();
        } catch (const std::exception& e) {
            current_failures().push_back(std::string("uncaught exception: ") + e.what());
        } catch (...) {
            current_failures().push_back("uncaught non-std::exception");
        }

        if (current_failures().empty()) {
            ++passed;
            std::cout << "[ OK ] " << tc.group << "." << tc.name << "\n";
        } else {
            failed_names.push_back(tc.group + "." + tc.name);
            std::cout << "[FAIL] " << tc.group << "." << tc.name << "\n";
            for (const auto& f : current_failures()) {
                std::cout << "       " << f << "\n";
            }
        }
    }

    std::cout << "\n==================================================\n";
    std::cout << passed << "/" << total << " tests passed\n";
    if (!failed_names.empty()) {
        std::cout << "FAILED:\n";
        for (const auto& n : failed_names) {
            std::cout << "  - " << n << "\n";
        }
    }
    std::cout << "==================================================\n";

    return failed_names.empty() ? 0 : 1;
}

} // namespace msgframe_test

// TEST(group, name) реєструє лямбду в глобальному реєстрі через
// статичний об'єкт Registrar, що виконується до входу в main()
// (стандартний паттерн self-registration, як у GoogleTest/Catch2).
#define TEST(group, name) \
    static void msgframe_test_##group##_##name(); \
    static ::msgframe_test::Registrar msgframe_registrar_##group##_##name( \
        #group, #name, msgframe_test_##group##_##name); \
    static void msgframe_test_##group##_##name()

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            ::msgframe_test::record_failure(#expr, __FILE__, __LINE__); \
        } \
    } while (0)

#define CHECK_EQ(actual, expected) \
    do { \
        if (!((actual) == (expected))) { \
            ::msgframe_test::record_failure_eq((actual), (expected), \
                #actual " == " #expected, __FILE__, __LINE__); \
        } \
    } while (0)

#define CHECK_FALSE(expr) CHECK(!(expr))
#define CHECK_NULL(ptr) CHECK((ptr) == nullptr)
#define CHECK_NOT_NULL(ptr) CHECK((ptr) != nullptr)
