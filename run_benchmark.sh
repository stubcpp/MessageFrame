#!/bin/bash
# На Linux / macOS: Відкрийте термінал у папці проєкту.
# Дайте скрипту права на виконання: chmod +x run_benchmark.sh.
# Запустіть його: ./run_benchmark.sh [--iterations N] [--params N].

echo "=================================================="
echo "   LOW-LATENCY LIBRARY AUTO BUILD AND RUN (UNIX)  "
echo "=================================================="
echo ""

# --- ПЕРЕВІРКА НАЯВНОСТІ GIT ТА СУБМОДУЛІВ ---
echo "[INFO] Synchronizing Git Submodules..."
if command -v git >/dev/null 2>&1; then
    git submodule update --init --recursive
else
    echo "[WARNING] Git not found. Skipping submodule update."
fi

# Назва папки для збірки
BUILD_DIR="build"

# Якщо папка існує, чистимо її
if [ -d "$BUILD_DIR" ]; then
    echo "[INFO] Removing old build cache..."
    rm -rf "$BUILD_DIR"
fi

# --- ПЕРЕВІРКА НАЯВНОСТІ CMAKE ---
echo "[1/3] Generating CMake project configuration (Release)..."
if ! command -v cmake >/dev/null 2>&1; then
    echo "[ERROR] CMake is not found on your system!"
    echo "Please install CMake using your package manager (e.g., 'sudo apt install cmake' or 'brew install cmake')."
    exit 1
fi

# Для GCC/Clang тип збірки Release потрібно вказувати безпосередньо при генерації.
# Тести/example нам тут не потрібні — лишаємо лише бенчмарк-таргет.
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DMSGFRAME_BUILD_EXAMPLES=OFF \
    -DMSGFRAME_BUILD_TESTS=OFF
if [ $? -ne 0 ]; then
    echo "[ERROR] CMake project generation failed!"
    exit 1
fi

echo ""
echo "[2/3] Compiling the project with Makefile (O3 / march=native)..."
# Збираємо проєкт, задіюючи всі доступні ядра процесора за допомогою -j
# Визначаємо кількість ядер (nproc для Linux, sysctl для macOS, інакше 2)
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

# Запуск виконуваного файлу. Будь-які аргументи скрипта (--iterations N,
# --params N) прокидаються напряму в бенчмарк, напр.:
#   ./run_benchmark.sh --params 150 --iterations 50000
if [ -f "$BUILD_DIR/messageframe_benchmark" ]; then
    "./$BUILD_DIR/messageframe_benchmark" "$@"
else
    echo "[ERROR] Executable file messageframe_benchmark not found!"
    exit 1
fi

echo ""
echo "--------------------------------------------------"
echo "Benchmark completed."
