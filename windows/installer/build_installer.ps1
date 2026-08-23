$ErrorActionPreference = 'Stop'

$RepoRoot = Resolve-Path "$PSScriptRoot\..\.."
$ElectronGuiDir = "$RepoRoot\windows\electron-gui"

# Step 1: Build C++ backend and ASIO driver (if not already built)
$BackendDll = "$RepoRoot\windows\build\bin\Release\iphone_mic_backend.dll"
$AsioDll = "$RepoRoot\windows\build\bin\Release\iphone_asio_driver.dll"

Write-Host "Building C++ backend..."
Push-Location "$RepoRoot\windows"
if (-not (Test-Path build)) { mkdir build | Out-Null }
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
Pop-Location

# Step 2: Build Electron + React GUI
Write-Host "Building Electron GUI..."
Push-Location $ElectronGuiDir
npm run build
Pop-Location

# Step 3: Package with electron-builder (NSIS installer)
Write-Host "Packaging installer..."
Push-Location $ElectronGuiDir
npx electron-builder --win --config electron-builder.yml
Pop-Location

Write-Host "========================================"
Write-Host "Installer created successfully!"
Write-Host "Path: $RepoRoot\dist\"
Write-Host "========================================"
