# Installation & Build Guide

This library is self-contained and uses Git submodules for its two
dependencies (`msgpack-c` and `tsl::robin_map`), so no system-wide package
manager (and no Boost) is required.

## Prerequisites

- **Git** — to clone the repository and fetch the submodules
  (`msgpack-c`, `tsl::robin_map`). Without it, `third_party/` stays empty
  and the build fails.
- **CMake 3.14 or newer.**
- **A C++17 compiler:**
  - *Windows* — Visual Studio 2019 or newer, with the "Desktop development
    with C++" workload (this also bundles a compatible CMake, which the
    `.bat` script can find automatically — see below).
  - *Linux* — GCC 7+ or Clang 5+ (e.g. the `build-essential` package).
  - *macOS* — Xcode Command Line Tools (`xcode-select --install`).

## 1. Cloning the repository

```bash
git clone --recursive https://github.com/stubcpp/MessageFrame.git
cd MessageFrame
```

If you already cloned without `--recursive`, fetch the submodules separately:

```bash
git submodule update --init --recursive
```

## 2. Building

### Method 1: Helper scripts (quick build + benchmark)

If you just cloned the repository and want to verify performance
immediately without running multiple commands, use the built-in helper
scripts: `run_benchmark.bat` (Windows) or `run_benchmark.sh` (Linux/macOS).

These scripts handle the entire setup sequence:
1. **Submodule verification** — runs `git submodule update --init --recursive` if `third_party/` is empty.
2. **Environment configuration** — locates a valid toolchain and sets up a clean build directory.
3. **Release build** — compiles the project in Release mode using all available CPU cores.
4. **Execution** — runs the compiled binary and forwards any command-line arguments to it.

**Windows (Visual Studio / MSVC):**
```cmd
run_benchmark.bat --params 4 --iterations 50000
```

**Linux / macOS (GCC / Clang):**
```bash
chmod +x run_benchmark.sh
./run_benchmark.sh --params 4 --iterations 50000
```

### Method 2: Manual CMake build

If you prefer full control over your compilation flags, or need to build
manually without the helper scripts, make sure you pull the dependencies
first:

```bash
git submodule update --init --recursive
```

Always compile in **Release mode**. A Debug build introduces heavy STL
iterator validation and extra bounds checking that noticeably skews
performance measurements.

**Windows (Visual Studio / MSVC)** — from a terminal or Developer Command
Prompt for VS:
```cmd
cmake -B build
cmake --build build --config Release
```

**Linux / macOS (GCC / Clang):**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j$(nproc)
```

By default the build produces the library plus examples, benchmarks, and
tests:

```bash
# Linux / macOS
./build/messageframe_example
./build/messageframe_extended_example
./build/messageframe_benchmark --iterations 50000 --params 4
./build/test_hybrid_map
./build/test_message_frame
./build/test_flat_key
./build/test_messageframe_parameter_api

# Windows
.\build\Release\messageframe_example.exe
.\build\Release\messageframe_extended_example.exe
.\build\Release\messageframe_benchmark.exe --iterations 50000 --params 4
.\build\Release\test_hybrid_map.exe
.\build\Release\test_message_frame.exe
.\build\Release\test_flat_key.exe
.\build\Release\test_messageframe_parameter_api.exe
```

There's no single combined test binary — each test file in `tests/`
builds its own executable so `ctest` can report failures per module. Run
them all at once with `ctest --test-dir build` (or just `ctest` from
inside `build/`).

Examples, benchmarks, and tests are each optional and can be disabled at
configure time, e.g. `cmake -B build -DMSGFRAME_BUILD_TESTS=OFF`
(see `MSGFRAME_BUILD_EXAMPLES` / `MSGFRAME_BUILD_BENCHMARKS` /
`MSGFRAME_BUILD_TESTS` in `CMakeLists.txt`).

### Method 3: CMake `FetchContent`

To pull MessageFrame directly into your own project at configure-time, add
this to your top-level `CMakeLists.txt`:

```cmake
include(FetchContent)

FetchContent_Declare(
    MessageFrame
    GIT_REPOSITORY https://github.com/stubcpp/MessageFrame
    GIT_TAG        master # Replace with a specific release tag or commit hash for stability
)

# Ensure vendored submodule dependencies are fetched too
FetchContent_GetProperties(MessageFrame)
if(NOT messageframe_POPULATED)
    FetchContent_Populate(MessageFrame)
    execute_process(
        COMMAND git submodule update --init --recursive
        WORKING_DIRECTORY ${messageframe_SOURCE_DIR}
    )
    add_subdirectory(${messageframe_SOURCE_DIR} ${messageframe_BINARY_DIR})
endif()

# The library target defined by CMakeLists.txt is `msg_frame`, not
# `MessageFrame` (that's just the project() name).
target_link_libraries(your_project_target PRIVATE msg_frame)
```

The repository's `CMakeLists.txt` doesn't currently export an installed
package config (its `install()`/`export()` block is commented out), so
`find_package(MessageFrame)` isn't available yet — `add_subdirectory` is
the supported integration path for now.

### Method 4: Manual source integration (no build system)

Because MessageFrame is standard, portable C++17 code, you can bypass
external build tools entirely and embed the source directly into your
tree.

1. Clone the repository recursively to fetch the vendor headers:
   ```bash
   git clone --recursive https://github.com/stubcpp/MessageFrame
   ```
2. Copy the folders into your project structure:
   - Copy `include/messageframe/` into your project's header directory.
   - Copy the implementation files from `src/` (`Header.cpp`, `Value.cpp`,
     `HybridMessageMap.cpp`, `MessageFrame.cpp`) into your source tree.
   - Copy `third_party/msgpack` and `third_party/robin_map` into your
     internal vendor paths.
3. Update your build configuration to point at the copied directories and
   compile the four `.cpp` files.

**Custom CMake:**
```cmake
target_include_directories(your_project_target PRIVATE
    path/to/include
    path/to/third_party/msgpack/include
    path/to/third_party/robin_map/include
)

target_sources(your_project_target PRIVATE
    path/to/src/Header.cpp
    path/to/src/Value.cpp
    path/to/src/HybridMessageMap.cpp
    path/to/src/MessageFrame.cpp
)
```

**Visual Studio IDE:**
1. Project → Properties → C/C++ → General → Additional Include
   Directories: add paths to your copied `include/`,
   `third_party/msgpack/include/`, and `third_party/robin_map/include/`.
2. Solution Explorer → Add → Existing Item... → select the four `.cpp`
   files from `src/`.
