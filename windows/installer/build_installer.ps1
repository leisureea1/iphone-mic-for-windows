$ErrorActionPreference = 'Stop'

$RepoRoot = Resolve-Path "$PSScriptRoot\..\.."
$WpfProject = "$RepoRoot\windows\WpfGUI\iPhoneMic.csproj"
$SetupScript = "$RepoRoot\windows\installer\setup.iss"

Write-Host "Publishing WPF Application (Self-Contained)..."
# Publish self-contained so users don't need .NET 9.0 installed
dotnet publish $WpfProject -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true

# Also copy ASIO driver to the publish directory so it gets packed
Write-Host "Copying native DLLs..."
$PublishDir = "$RepoRoot\windows\WpfGUI\bin\Release\net9.0-windows\win-x64\publish"
Copy-Item "$RepoRoot\windows\build\bin\Release\iphone_asio_driver.dll" -Destination $PublishDir
Copy-Item "$RepoRoot\windows\build\bin\Release\iphone_mic_backend.dll" -Destination $PublishDir

Write-Host "Checking for Inno Setup..."
$InnoPaths = @(
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
)

$InnoPath = $null
foreach ($path in $InnoPaths) {
    if (Test-Path $path) {
        $InnoPath = $path
        break
    }
}

if (-not $InnoPath) {
    Write-Host "Inno Setup not found. Attempting to install via winget..."
    winget install -e --id JRSoftware.InnoSetup --accept-package-agreements --accept-source-agreements
    
    foreach ($path in $InnoPaths) {
        if (Test-Path $path) {
            $InnoPath = $path
            break
        }
    }
    
    if (-not $InnoPath) {
        Write-Error "Failed to locate Inno Setup after installation. Please install it manually from https://jrsoftware.org/isdl.php"
        exit 1
    }
}

Write-Host "Compiling Installer..."
& $InnoPath $SetupScript

Write-Host "========================================"
Write-Host "Installer created successfully!"
Write-Host "Path: $RepoRoot\dist\iPhoneMic_Setup.exe"
Write-Host "========================================"
