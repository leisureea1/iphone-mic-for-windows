@echo off
REM ========================================
REM iPhone USB Microphone - Build Script
REM ========================================
REM Prerequisites:
REM   - Visual Studio 2022 (with C++ desktop workload)
REM   - CMake 3.25+ (included with VS 2022)
REM ========================================

echo.
echo ========================================
echo  iPhone USB Microphone - Build
echo ========================================
echo.

REM Check for CMake
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: CMake not found in PATH
    echo Please install CMake 3.25+ or use Visual Studio Developer Command Prompt
    exit /b 1
)

REM Navigate to windows directory
pushd %~dp0\..\windows

REM Create build directory
if not exist build mkdir build

REM Configure with CMake
echo [1/3] Configuring CMake...
cmake -B build -G "Visual Studio 17 2022" -A x64
if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed
    popd
    exit /b 1
)

REM Build Release
echo.
echo [2/3] Building Release...
cmake --build build --config Release --parallel
if %errorlevel% neq 0 (
    echo ERROR: Build failed
    popd
    exit /b 1
)

REM Build Debug (for development)
echo.
echo [3/3] Building Debug...
cmake --build build --config Debug --parallel
if %errorlevel% neq 0 (
    echo WARNING: Debug build failed (Release build succeeded)
)

echo.
echo ========================================
echo  Build Complete!
echo ========================================
echo.
echo Output files:
echo   Client:      build\bin\Release\iphone_mic_client.exe
echo   ASIO Driver: build\bin\Release\iphone_asio_driver.dll
echo   Tests:       build\bin\Release\ring_buffer_test.exe
echo                build\bin\Release\protocol_test.exe
echo                build\bin\Release\audio_format_test.exe
echo.
echo Next steps:
echo   1. Run tests:  build\bin\Release\ring_buffer_test.exe
echo   2. Register ASIO driver (admin required):
echo      regsvr32 build\bin\Release\iphone_asio_driver.dll
echo   3. Start iproxy:  iproxy 8730 8730
echo   4. Run client:  build\bin\Release\iphone_mic_client.exe
echo.

popd
pause
