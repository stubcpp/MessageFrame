cmake_minimum_required(VERSION 3.14)
project(HighPerfMessageLibrary VERSION 3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Global compiler optimizations for ALL targets in the project (library, tests, etc.)
if(MSVC)
    # /MP - multi-threaded build (always safe)
    # /utf-8 - Unicode support (always safe)
    # /permissive- - strict C++ standard (always safe)
    add_compile_options(/MP /utf-8 /permissive- /W4) 

    # Optimizations /O2 and /Oi are enabled STRICTLY for Release or MinSizeRel configurations
    add_compile_options($<$<CONFIG:Release,MinSizeRel>:/O2> $<$<CONFIG:Release,MinSizeRel>:/Oi>)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options($<$<CONFIG:Release,MinSizeRel>:-O3> $<$<CONFIG:Release,MinSizeRel>:-march=native>)
endif()

# Local paths to submodules in third_party (according to your folders)
set(ROBIN_MAP_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/robin_map/include")
set(MSGPACK_INCLUDE_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/third_party/msgpack/include")

# ------------------------------------------------------------------
# LIBRARY
# ------------------------------------------------------------------

# Option: build static or shared library
# Default is STATIC, but SHARED can be enabled:
option(MSGFRAME_BUILD_SHARED "Build shared library instead of static" OFF)

if(MSGFRAME_BUILD_SHARED)
    # If the user enables this option, a DLL/so will be created
    add_library(msg_frame SHARED
        src/Value.cpp
        src/Header.cpp
        src/HybridMessageMap.cpp
        src/MessageFrame.cpp
    )
else()
    # By default — static library
    add_library(msg_frame STATIC
        src/Value.cpp
        src/Header.cpp
        src/HybridMessageMap.cpp
        src/MessageFrame.cpp
    )
endif()

# PUBLIC: The user only sees our include folder
# (include/messageframe/*.hpp -> #include <messageframe/Xxx.hpp>)
target_include_directories(msg_frame PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# PRIVATE: Dependencies are needed ONLY for compilation inside src/
target_include_directories(msg_frame PRIVATE
    ${ROBIN_MAP_INCLUDE_DIR}
    ${MSGPACK_INCLUDE_DIR}
)

# Disable MsgPack dependency on Boost globally for the entire project.
# PUBLIC ensures the macro applies to all targets linking msg_frame
# (examples, benchmarks, tests), not only the library itself.
target_compile_definitions(msg_frame PUBLIC MSGPACK_NO_BOOST MSGPACK_DISABLE_BOOST)

# Windows-specific settings to protect against min/max macros (propagated everywhere
# that links msg_frame, thanks to INTERFACE)
if(WIN32)
    target_compile_definitions(msg_frame INTERFACE NOMINMAX)
endif()

# ------------------------------------------------------------------
# EXAMPLES — minimal API demonstration, without timing measurements.
# Built by default; disable with: -DMSGFRAME_BUILD_EXAMPLES=OFF
# ------------------------------------------------------------------
option(MSGFRAME_BUILD_EXAMPLES "Build messageframe usage examples" ON)

if(MSGFRAME_BUILD_EXAMPLES)
    add_executable(messageframe_example examples/basic_usage.cpp)
    target_link_libraries(messageframe_example PRIVATE msg_frame)
endif()

# ------------------------------------------------------------------
# BENCHMARKS — performance measurements (add/serialize/deserialize).
# Built by default; disable with: -DMSGFRAME_BUILD_BENCHMARKS=OFF
# ------------------------------------------------------------------
option(MSGFRAME_BUILD_BENCHMARKS "Build messageframe performance benchmarks" ON)

if(MSGFRAME_BUILD_BENCHMARKS)
    add_executable(messageframe_benchmark benchmarks/benchmark.cpp)
    target_link_libraries(messageframe_benchmark PRIVATE msg_frame)
endif()

# ------------------------------------------------------------------
# TESTS — unit tests for correctness (zero-dependency harness).
# Built by default; disable with: -DMSGFRAME_BUILD_TESTS=OFF
# enable_testing()/add_test() allow running via `ctest`.
# ------------------------------------------------------------------
option(MSGFRAME_BUILD_TESTS "Build messageframe unit tests" ON)

if(MSGFRAME_BUILD_TESTS)
    enable_testing()

    add_executable(messageframe_tests tests/test_hybrid_map.cpp)
    target_link_libraries(messageframe_tests PRIVATE msg_frame)
    # tests/test_framework.hpp is located next to the .cpp itself —
    # no additional include_directories are needed, because #include "test_framework.hpp"
    # resolves relative to the directory of the file that includes it.

    add_test(NAME HybridMessageMapTests COMMAND messageframe_tests)
endif()

# ------------------------------------------------------------------
# INSTALL (commented out)
# ------------------------------------------------------------------
# Below is an example install() section for distributing the library.
# Users can uncomment if they want to install msg_frame
# into system directories (e.g., /usr/local or Program Files).
#
# install(TARGETS msg_frame EXPORT msg_frameTargets
#     ARCHIVE DESTINATION lib
#     LIBRARY DESTINATION lib
#     RUNTIME DESTINATION bin
# )
#
# install(DIRECTORY include/ DESTINATION include)
#
# export(EXPORT msg_frameTargets
#     FILE "${CMAKE_CURRENT_BINARY_DIR}/msg_frameTargets.cmake"
#     NAMESPACE msg_frame::
# )
#
# install(EXPORT msg_frameTargets
#     FILE msg_frameTargets.cmake
#     NAMESPACE msg_frame::
#     DESTINATION lib/cmake/msg_frame
# )
#
# # This will allow other projects to use find_package(msg_frame CONFIG)
