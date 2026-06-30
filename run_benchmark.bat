:: На Windows: Просто двічі клацніть на файл run_benchmark.bat.
:: Скрипт сам усе підготує та виведе результати швидкості в консоль.
:: Можна також передати аргументи бенчмарку, напр.:
::   run_benchmark.bat --params 150 --iterations 50000

@echo off
setlocal enabledelayedexpansion

echo ==================================================
echo   LOW-LATENCY LIBRARY AUTO BUILD AND RUN (WIN)   
echo ==================================================
echo.

:: --- ШУКАЄМО CMAKE ВІД VISUAL STUDIO ---
echo [INFO] Searching for CMake in Visual Studio...

:: Шукаємо шлях до інсталяції Visual Studio за допомогою vswhere
set "VS_PATH="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)

if defined VS_PATH (
    :: Формуємо шлях до вбудованого CMake (зазвичай для VS 2019/2022 він лежить тут)
    set "VS_CMAKE_PATH=!VS_PATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    
    if exist "!VS_CMAKE_PATH!\cmake.exe" (
        echo [INFO] Found Visual Studio CMake at: !VS_CMAKE_PATH!
        :: Додаємо цей шлях на початок змінної PATH тільки для цього сеансу скрипта
        set "PATH=!VS_CMAKE_PATH!;!PATH!"
    ) else (
        echo [WARNING] VS found, but internal cmake.exe was not found in standard subfolder.
    )
) else (
    echo [WARNING] Visual Studio not detected via vswhere. Trying global cmake...
)
:: -------------------------------------

:: Автоматично викачуємо субмодулі, якщо користувач забув зробити це при git clone
echo [INFO] Synchronizing Git Submodules...
:: Перевіримо чи є git в PATH перед запуском, щоб скрипт не падав з помилкою
where git >nul 2>nul
if %errorlevel% equ 0 (
    git submodule update --init --recursive
) else (
    echo [WARNING] Git not found. Skipping submodule update.
)

:: Назва папки для збірки
set BUILD_DIR=build

:: Якщо папка збірки вже існує, видаляємо старий кеш для чистого старту
if exist %BUILD_DIR% (
    echo [INFO] Removing old build cache...
    rmdir /s /q %BUILD_DIR%
)

echo [1/3] Generating CMake project configuration...
:: Перевірка чи cmake тепер доступний
where cmake >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] CMake is not found globally or in Visual Studio!
    echo Please install CMake or Visual Studio C++ Development Workload.
    pause
    exit /b 1
)

:: Створюємо конфігурацію. MSVC підтримує мультиконфігурацію за замовчуванням.
:: Тести/example нам тут не потрібні — лишаємо лише бенчмарк-таргет.
cmake -B %BUILD_DIR% -DMSGFRAME_BUILD_EXAMPLES=OFF -DMSGFRAME_BUILD_TESTS=OFF
if %errorlevel% neq 0 (
    echo [ERROR] CMake project generation failed!
    pause
    exit /b %errorlevel%
)

echo.
echo [2/3] Compiling the project in Release mode (MSVC)...
:: Обов'язково вказуємо --config Release, щоб увімкнути оптимізації /O2
:: (CMAKE_BUILD_TYPE ігнорується мультиконфігураційним генератором Visual Studio —
:: конфігурація вибирається лише через --config саме на цьому кроці).
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

:: Запуск виконуваного файлу. Будь-які аргументи скрипта (--iterations N,
:: --params N) прокидаються напряму в бенчмарк через %*.
if exist %BUILD_DIR%\Release\messageframe_benchmark.exe (
    %BUILD_DIR%\Release\messageframe_benchmark.exe %*
) else (
    echo [ERROR] Executable file messageframe_benchmark.exe not found!
)

echo.
echo --------------------------------------------------
echo Benchmark completed.
pause
