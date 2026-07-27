@echo off
REM ========================================
REM iPhone USB Microphone - ASIO Unregister
REM ========================================
echo.
echo ========================================
echo  iPhone USB Microphone ASIO Uninstaller
echo ========================================
echo.

REM Check for administrator privileges
net session >nul 2>&1
if %errorLevel% == 0 (
    echo [INFO] Running with Administrator privileges.
) else (
    echo [ERROR] This script requires Administrator privileges.
    echo Please right-click and select "Run as administrator".
    echo.
    pause
    exit /b 1
)

set DLL_PATH=%~dp0iphone_asio_driver.dll
if not exist "%DLL_PATH%" (
    REM Try relative to development build directory if not in the same folder
    set DLL_PATH=%~dp0..\build\bin\Release\iphone_asio_driver.dll
)

if not exist "%DLL_PATH%" (
    echo [ERROR] Could not find iphone_asio_driver.dll
    echo Please make sure the DLL is in the same folder as this script.
    echo.
    pause
    exit /b 1
)

echo [INFO] Unregistering ASIO Driver...
echo DLL Path: %DLL_PATH%
echo.

regsvr32 /u /s "%DLL_PATH%"

if %errorLevel% == 0 (
    echo [SUCCESS] iPhone USB Microphone ASIO driver unregistered successfully!
    echo It will no longer appear in your DAW.
) else (
    echo [ERROR] Failed to unregister the driver. Error code: %errorLevel%
)

echo.
pause
