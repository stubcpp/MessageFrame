# Installation & Build Guide

This library is self-contained and uses Git submodules for its two
dependencies (`msgpack-c` and `tsl::robin_map`), so no system-wide package
managers (and no Boost templates) are required.

## Prerequisites

To compile and link the library, ensure your development workspace meets the following minimum baselines:

* 🐙 **Git** — Required to clone the source tree and pull the third-party submodules. Without it, the `third_party/` directory stays empty and compilation flags will fail.
* 🛠️ **CMake 3.14 or newer** — Handles build pipeline generation.
* 💻 **A Compliant C++17 Compiler:**
  * **Windows** — Visual Studio 2019 or newer, with the *"Desktop development with C++"* workload configured.
  * **Linux** — GCC 7+ or Clang 5+ (e.g., via the standard `build-essential` tracking metadata package).
  * **macOS** — Xcode Command Line Tools (`xcode-select --install`).

## 1. Cloning the repository

To check out the repository along with its pinned third-party targets, pull recursively:

```bash
git clone --recursive https://github.com
cd MessageFrame
```

If you accidentally cloned the project without the `--recursive` flag, initialize the tracking links manually before running your configuration steps:

```bash
git submodule update --init --recursive
```

## 2. Build and Integration Methods

### Method 1: Automated Turnkey Helper Scripts (Quick Benchmark)

If you have just cloned the project and want to immediately verify
its runtime performance benchmarks without typing multiple commands,
use the built-in helper scripts: `run_benchmark.bat` (Windows) or `run_benchmark.sh` (Linux/macOS).


These scripts perform the full build cycle:
1. **Submodule Verification** — Checks if `third_party/` is populated; fetches submodules if missing.
2. **Environment Configuration** — Locates valid compilers and registers an isolated, clean build layout.
3. **Release Compilation** — Compiles the binaries in Release mode using all available CPU cores.
4. **Execution** — Runs the built benchmark framework and forwards downstream flags.

**Windows (Visual Studio / MSVC Terminal):**
```cmd
run_benchmark.bat --params 4 --iterations 200000
```

**Linux / macOS (Bash Shell):**
```bash
chmod +x run_benchmark.sh
./run_benchmark.sh --params 4 --iterations 200000
```

### Method 2: Manual CMake Workspace Build

If you prefer full control over your compilation flags, or need to build
manually without the helper scripts, make sure you pull the dependencies
first:

```bash
git submodule update --init --recursive
```

⚠️ **Crucial Rule:** Always target **Release mode** (`-DCMAKE_BUILD_TYPE=Release` or `--config Release`). Debug builds introduce heavy C++ STL iterator assertions and boundary checking layers that will severely skew micro-benchmarking measurements.

#### 🐧 Linux / macOS (GCC / Clang)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

#### 🪟 Windows (Visual Studio / MSVC)
Run from a standard terminal window or the Developer Command Prompt for Visual Studio:
```cmd
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Built Artifact Locations
By default, compiling the full workspace populates test frameworks, isolated micro-benchmarks, and usage examples. The resulting compiled binaries are mapped below:

```bash
# Linux / macOS Artifact Tree
./build/messageframe_basic_usage
./build/messageframe_extended_usage
./build/messageframe_benchmark --iterations 50000 --params 4
./build/messageframe_tests

# Windows Artifact Tree
.\build\Release\messageframe_basic_usage.exe
.\build\Release\messageframe_extended_example.exe
.\build\Release\messageframe_benchmark.exe --iterations 50000 --params 4
.\build\Release\messageframe_tests.exe
```
*Note: Targets can be selectively turned off during generation to speed up pipeline deployment, e.g., `cmake -B build -DMSGFRAME_BUILD_TESTS=OFF`.*

### Method 3: CMake `FetchContent` Integration

To pull MessageFrame directly into your own project at configure-time, add
this to your top-level `CMakeLists.txt`:

```cmake
include(FetchContent)

FetchContent_Declare(
    MessageFrame
    GIT_REPOSITORY https://github.com/stubcpp/MessageFrame
    GIT_TAG        master       # Replace with a specific release tag or commit hash for stability
    GIT_SUBMODULES_RECURSIVE ON # Automatically clones and initializes vendored dependencies (msgpack, robin_map)
)

# Fetch content and automatically expose target symbols
FetchContent_MakeAvailable(MessageFrame)

# Bind directly onto your application runtime target
target_link_libraries(your_project_target PRIVATE MessageFrame)
```

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

#### Visual Studio IDE (GUI-Driven Environments)
1. **Include Search Directories:** Open *Project ➔ Properties ➔ C/C++ ➔ General ➔ Additional Include Directories* and register the paths for your local copies of `include/`, `third_party/msgpack/include/`, and `third_party/robin_map/include/`.
2. **Link Code Files:** Inside the Solution Explorer tree, right-click, select *Add ➔ Existing Item...*, and select the four active translation engine files (`.cpp`) extracted from `src/`.
