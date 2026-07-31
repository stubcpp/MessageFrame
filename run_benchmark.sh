#!/bin/bash
# On Linux / macOS: Open a terminal in the project folder.
# Give the script executable permissions: chmod +x run_benchmark.sh.
# Run it: ./run_benchmark.sh [--iterations N] [--params N].

echo "=================================================="
echo "   LOW-LATENCY LIBRARY AUTO BUILD AND RUN (UNIX)  "
echo "=================================================="
echo ""

# --- CHECKING THE AVAILABILITY OF GIT AND SUBMODULES ---
echo "[INFO] Synchronizing Git Submodules..."
if command -v git >/dev/null 2>&1; then
    git submodule update --init --recursive
else
    echo "[WARNING] Git not found. Skipping submodule update."
fi

# Name of the folder for the building
BUILD_DIR="build"

# If the folder exists, clean it.
if [ -d "$BUILD_DIR" ]; then
    echo "[INFO] Removing old build cache..."
    rm -rf "$BUILD_DIR"
fi

# --- CHECKING CMAKE AVAILABILITY ---
echo "[1/3] Generating CMake project configuration (Release)..."
if ! command -v cmake >/dev/null 2>&1; then
    echo "[ERROR] CMake is not found on your system!"
    echo "Please install CMake using your package manager (e.g., 'sudo apt install cmake' or 'brew install cmake')."
    exit 1
fi

# For GCC/Clang, the Release build type must be specified directly during generation.
# We don't need tests/examples here - we only leave the benchmark target.
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DMSGFRAME_BUILD_EXAMPLES=OFF \
    -DMSGFRAME_BUILD_TESTS=OFF
if [ $? -ne 0 ]; then
    echo "[ERROR] CMake project generation failed!"
    exit 1
fi

echo ""
echo "[2/3] Compiling the project with Makefile (O3 / march=native)..."
# Build the project using all available processor cores using -j
# Determine the number of cores (nproc for Linux, sysctl for macOS, otherwise 2)
THREADS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)
cmake --build "$BUILD_DIR" --target messageframe_benchmark -- -j"$THREADS"
if [ $? -ne 0 ]; then
    echo "[ERROR] Compilation failed!"
    exit 1
fi

echo ""
echo "[3/3] Running the Performance Benchmark..."
echo "--------------------------------------------------"
echo ""

# Run the executable. Any script arguments (--iterations N,
# --params N) are passed directly to the benchmark, e.g.:
# ./run_benchmark.sh --params 150 --iterations 50000
if [ -f "$BUILD_DIR/messageframe_benchmark" ]; then
    "./$BUILD_DIR/messageframe_benchmark" "$@"
else
    echo "[ERROR] Executable file messageframe_benchmark not found!"
    exit 1
fi

echo ""
echo "--------------------------------------------------"
echo "Benchmark completed."
