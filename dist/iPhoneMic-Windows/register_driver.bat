@echo off
REM ========================================
REM iPhone USB Microphone - ASIO Driver Registration
REM ========================================
REM This script must be run as Administrator!
REM ========================================

echo.
echo ========================================
echo  ASIO Driver Registration
echo ========================================
echo.

REM Check for admin privileges
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: This script requires Administrator privileges.
    echo Right-click and select "Run as administrator"
    echo.
    pause
    exit /b 1
)

set DLL_PATH=%~dp0iphone_asio_driver.dll
if not exist "%DLL_PATH%" (
    REM Try relative to development tools directory
    set DLL_PATH=%~dp0..\build\bin\Release\iphone_asio_driver.dll
)

if not exist "%DLL_PATH%" (
    echo ERROR: ASIO driver DLL not found at:
    echo   %DLL_PATH%
    echo.
    echo Please build the project first:
    echo   scripts\build_all.bat
    echo.
    pause
    exit /b 1
)

echo Registering ASIO driver...
echo DLL: %DLL_PATH%
echo.

regsvr32 /s "%DLL_PATH%"
if %errorlevel% neq 0 (
    echo ERROR: Registration failed!
    echo Try running regsvr32 manually:
    echo   regsvr32 "%DLL_PATH%"
    pause
    exit /b 1
)

echo ========================================
echo  Registration Successful!
echo ========================================
echo.
echo The driver "iPhone USB Microphone ASIO" is now registered.
echo.
echo To verify:
echo   1. Open your DAW (Studio One, Cubase, etc.)
echo   2. Go to Audio Settings
echo   3. Select ASIO driver
echo   4. "iPhone USB Microphone ASIO" should appear in the list
echo.
echo To unregister:
echo   regsvr32 /u "%DLL_PATH%"
echo.
pause
