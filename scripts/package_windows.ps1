$ErrorActionPreference = 'Stop'

$RepoRoot = Resolve-Path "$PSScriptRoot\.."
$DistDir = "$RepoRoot\dist\iPhoneMic-Windows"
$ZipFile = "$RepoRoot\dist\iPhoneMic-Windows.zip"

Write-Host "Creating Windows Distribution Package..."

# Clean old dist directory
if (Test-Path "$RepoRoot\dist") {
    Remove-Item -Recurse -Force "$RepoRoot\dist"
}
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

# Verify build exists
$BuildBin = "$RepoRoot\windows\build\bin\Release"
if (-not (Test-Path $BuildBin)) {
    Write-Error "Release build not found at $BuildBin. Please run build_all.bat first."
    exit 1
}

# Copy Binaries
Write-Host "Copying binaries..."
Copy-Item "$BuildBin\iphone_mic_gui.exe" -Destination "$DistDir\iPhoneMic Control Center.exe"
Copy-Item "$BuildBin\iphone_asio_driver.dll" -Destination $DistDir

# Copy Scripts
Write-Host "Copying scripts..."
Copy-Item "$RepoRoot\windows\tools\register_driver.bat" -Destination $DistDir
Copy-Item "$RepoRoot\windows\tools\unregister_driver.bat" -Destination $DistDir

# Create a quick start guide
$ReadmePath = "$DistDir\README.txt"
@"
========================================
iPhone USB Microphone - Windows Client
========================================

How to use:
1. Connect your iPhone via USB.
2. Ensure you have Apple Mobile Device Support installed 
   (it comes with iTunes).
3. Right-click on "register_driver.bat" and select 
   "Run as administrator" to install the ASIO driver.
4. Open your DAW (Studio One, Cubase, Ableton, etc.) 
   and select "iPhone USB Microphone ASIO" in Audio Settings.

To manage the driver or test the connection:
- Open "iPhoneMic Control Center.exe"

From the Control Center, you can:
- See the connection status in real-time
- View your microphone's live waveform & VU meters
- Click "Install / Register ASIO Driver" with a single click

After registering, open your DAW (Studio One, Cubase, Ableton, etc.) 
and select "iPhone USB Microphone ASIO" in Audio Settings.
"@ | Out-File -FilePath $ReadmePath -Encoding UTF8

Write-Host "Zipping package..."
Compress-Archive -Path "$DistDir\*" -DestinationPath $ZipFile -Force

Write-Host ""
Write-Host "========================================"
Write-Host "Package created successfully!"
Write-Host "Path: $ZipFile"
Write-Host "========================================"
