cmake_minimum_required(VERSION 3.14)
project(HighPerfMessageLibrary VERSION 3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Глобальні оптимізації компілятора для ВСІХ цілей у проєкті (і ліби, і тестів)
if(MSVC)
    # /MP - багатопоточна збірка (завжди безпечна)
    # /utf-8 - підтримка юнікоду (завжди безпечна)
    # /permissive- - суворий стандарт C++ (завжди безпечна)
    add_compile_options(/MP /utf-8 /permissive- /W4) 

    # Оптимізації /O2 та /Oi вмикаються СТРОГО для конфігурацій Release або MinSizeRel
    add_compile_options($<$<CONFIG:Release,MinSizeRel>:/O2> $<$<CONFIG:Release,MinSizeRel>:/Oi>)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options($<$<CONFIG:Release,MinSizeRel>:-O3> $<$<CONFIG:Release,MinSizeRel>:-march=native>)
endif()

# Локальні шляхи до submodules в third_party (відповідно до твоїх папок)
set(ROBIN_MAP_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/robin_map/include")
set(MSGPACK_INCLUDE_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/third_party/msgpack/include")

# ------------------------------------------------------------------
# БІБЛІОТЕКА
# ------------------------------------------------------------------

# Створення СТАТИЧНОЇ бібліотеки — ВКЛЮЧАЄМО ВСІ ФАЙЛИ РЕАЛІЗАЦІЇ
add_library(msg_frame STATIC
    src/Value.cpp
    src/Header.cpp
    src/HybridMessageMap.cpp
    src/MessageFrame.cpp
)

# PUBLIC: Користувач бачить тільки нашу папку include
# (include/messageframe/*.hpp -> #include <messageframe/Xxx.hpp>)
target_include_directories(msg_frame PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# PRIVATE: Залежності потрібні ТІЛЬКИ для компіляції всередині src/
target_include_directories(msg_frame PRIVATE
    ${ROBIN_MAP_INCLUDE_DIR}
    ${MSGPACK_INCLUDE_DIR}
)

# Вимикаємо залежність MsgPack від Boost глобально для всього проєкту.
# PUBLIC гарантує, що макрос застосується до всіх таргетів, що лінкують msg_frame
# (examples, benchmarks, tests), а не лише до самої бібліотеки.
target_compile_definitions(msg_frame PUBLIC MSGPACK_NO_BOOST MSGPACK_DISABLE_BOOST)

# Windows-специфічні налаштування захисту від макросів min/max (прокидаються всюди,
# що лінкує msg_frame, завдяки INTERFACE)
if(WIN32)
    target_compile_definitions(msg_frame INTERFACE NOMINMAX)
endif()

# ------------------------------------------------------------------
# EXAMPLES — мінімальна демонстрація API, без замірів часу.
# Будуються за умовчанням; вимкнути: -DMSGFRAME_BUILD_EXAMPLES=OFF
# ------------------------------------------------------------------
option(MSGFRAME_BUILD_EXAMPLES "Build messageframe usage examples" ON)

if(MSGFRAME_BUILD_EXAMPLES)
    add_executable(messageframe_example examples/basic_usage.cpp)
    target_link_libraries(messageframe_example PRIVATE msg_frame)
endif()

# ------------------------------------------------------------------
# BENCHMARKS — заміри продуктивності (add/serialize/deserialize).
# Будуються за умовчанням; вимкнути: -DMSGFRAME_BUILD_BENCHMARKS=OFF
# ------------------------------------------------------------------
option(MSGFRAME_BUILD_BENCHMARKS "Build messageframe performance benchmarks" ON)

if(MSGFRAME_BUILD_BENCHMARKS)
    add_executable(messageframe_benchmark benchmarks/benchmark.cpp)
    target_link_libraries(messageframe_benchmark PRIVATE msg_frame)
endif()

# ------------------------------------------------------------------
# TESTS — юніт-тести коректності (zero-dependency harness).
# Будуються за умовчанням; вимкнути: -DMSGFRAME_BUILD_TESTS=OFF
# enable_testing()/add_test() дозволяють запускати через `ctest`.
# ------------------------------------------------------------------
option(MSGFRAME_BUILD_TESTS "Build messageframe unit tests" ON)

if(MSGFRAME_BUILD_TESTS)
    enable_testing()

    add_executable(messageframe_tests tests/test_hybrid_map.cpp)
    target_link_libraries(messageframe_tests PRIVATE msg_frame)
    # tests/test_framework.hpp лежить поруч із самим .cpp — додаткові
    # include_directories не потрібні, бо #include "test_framework.hpp"
    # резолвиться відносно директорії файлу, що його інклюдить.

    add_test(NAME HybridMessageMapTests COMMAND messageframe_tests)
endif()
