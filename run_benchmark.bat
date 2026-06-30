:: On Windows: Just double-click the file run_benchmark.bat.
:: The script will prepare everything and print performance results to the console.
:: You can also pass benchmark arguments, e.g.:
::   run_benchmark.bat --params 150 --iterations 50000

@echo off
setlocal enabledelayedexpansion

echo ==================================================
echo   LOW-LATENCY LIBRARY AUTO BUILD AND RUN (WIN)   
echo ==================================================
echo.

:: --- SEARCHING FOR CMAKE IN VISUAL STUDIO ---
echo [INFO] Searching for CMake in Visual Studio...

:: Locate Visual Studio installation path using vswhere
set "VS_PATH="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)

if defined VS_PATH (
    :: Build path to bundled CMake (usually for VS 2019/2022 it is here)
    set "VS_CMAKE_PATH=!VS_PATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    
    if exist "!VS_CMAKE_PATH!\cmake.exe" (
        echo [INFO] Found Visual Studio CMake at: !VS_CMAKE_PATH!
        :: Add this path to the beginning of PATH only for this script session
        set "PATH=!VS_CMAKE_PATH!;!PATH!"
    ) else (
        echo [WARNING] VS found, but internal cmake.exe was not found in the standard subfolder.
    )
) else (
    echo [WARNING] Visual Studio not detected via vswhere. Trying global cmake...
)
:: -------------------------------------

:: Automatically fetch submodules if the user forgot to do it during git clone
echo [INFO] Synchronizing Git Submodules...
:: Check if git is in PATH before running, so the script doesn’t crash
where git >nul 2>nul
if %errorlevel% equ 0 (
    git submodule update --init --recursive
) else (
    echo [WARNING] Git not found. Skipping submodule update.
)

:: Build folder name
set BUILD_DIR=build

:: If the build folder already exists, remove old cache for a clean start
if exist %BUILD_DIR% (
    echo [INFO] Removing old build cache...
    rmdir /s /q %BUILD_DIR%
)

echo [1/3] Generating CMake project configuration...
:: Check if cmake is now available
where cmake >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] CMake is not found globally or in Visual Studio!
    echo Please install CMake or Visual Studio C++ Development Workload.
    pause
    exit /b 1
)

:: Create configuration. MSVC supports multi-configuration by default.
:: Tests/examples are not needed here — only the benchmark target.
cmake -B %BUILD_DIR% -DMSGFRAME_BUILD_EXAMPLES=OFF -DMSGFRAME_BUILD_TESTS=OFF
if %errorlevel% neq 0 (
    echo [ERROR] CMake project generation failed!
    pause
    exit /b %errorlevel%
)

echo.
echo [2/3] Compiling the project in Release mode (MSVC)...
:: Always specify --config Release to enable /O2 optimizations
:: (CMAKE_BUILD_TYPE is ignored by Visual Studio multi-config generator —
:: configuration is selected only via --config at this step).
cmake --build %BUILD_DIR% --config Release --target messageframe_benchmark
if %errorlevel% neq 0 (
    echo [ERROR] Compilation failed!
    pause
    exit /b %errorlevel%
)

echo.
echo [3/3] Running the Performance Benchmark...
echo --------------------------------------------------
echo.

:: Run the executable. Any script arguments (--iterations N,
:: --params N) are passed directly to the benchmark via %*.
if exist %BUILD_DIR%\Release\messageframe_benchmark.exe (
    %BUILD_DIR%\Release\messageframe_benchmark.exe %*
) else (
    echo [ERROR] Executable file messageframe_benchmark.exe not found!
)

echo.
echo --------------------------------------------------
echo Benchmark completed.
pause
