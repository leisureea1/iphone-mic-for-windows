[Setup]
AppName=iPhone Mic
AppVersion=1.0.0
DefaultDirName={autopf}\iPhoneMic
DefaultGroupName=iPhone Mic
UninstallDisplayIcon={app}\iPhoneMic.exe
Compression=lzma2
SolidCompression=yes
OutputDir=..\..\dist
OutputBaseFilename=iPhoneMic_Setup
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
Source: "..\WpfGUI\bin\Release\net9.0-windows\win-x64\publish\iPhoneMic.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\WpfGUI\bin\Release\net9.0-windows\win-x64\publish\iphone_mic_backend.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\WpfGUI\bin\Release\net9.0-windows\win-x64\publish\iphone_asio_driver.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\WpfGUI\bin\Release\net9.0-windows\win-x64\publish\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\iPhone Mic Control Center"; Filename: "{app}\iPhoneMic.exe"
Name: "{group}\Uninstall iPhone Mic"; Filename: "{uninstallexe}"
Name: "{autodesktop}\iPhone Mic"; Filename: "{app}\iPhoneMic.exe"; Tasks: desktopicon

[Run]
Filename: "regsvr32.exe"; Parameters: "/s ""{app}\iphone_asio_driver.dll"""; StatusMsg: "Registering ASIO Driver..."
Filename: "{app}\iPhoneMic.exe"; Description: "Launch iPhone Mic Control Center"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "regsvr32.exe"; Parameters: "/u /s ""{app}\iphone_asio_driver.dll"""; StatusMsg: "Unregistering ASIO Driver..."

[Code]
function InitializeSetup(): Boolean;
begin
  Result := True;
end;
