#ifndef AppSource
  #error AppSource must point to the prepared FlowSpace package directory
#endif

#ifndef OutputDir
  #define OutputDir "build"
#endif

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

#ifndef OutputBaseFilename
  #define OutputBaseFilename "FlowSpace-1.0.0-windows-x64-setup"
#endif

[Setup]
AppId={{B1E4F84D-7B7B-4A45-9E9B-4B4B5A7C9C12}
AppName=FlowSpace
AppVersion={#AppVersion}
AppPublisher=FlowSpace contributors
AppPublisherURL=https://github.com/Anurag123Chauhan/FlowSpace
DefaultDirName={autopf}\FlowSpace
DefaultGroupName=FlowSpace
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64
ArchitecturesAllowed=x64
PrivilegesRequired=lowest
LicenseFile={#AppSource}\LICENSE
SetupIconFile={#SourcePath}\..\build\flowspace.ico
UninstallDisplayIcon={app}\flowspace.exe

[Files]
Source: "{#AppSource}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\FlowSpace"; Filename: "{app}\flowspace.exe"
Name: "{autodesktop}\FlowSpace"; Filename: "{app}\flowspace.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"

[Run]
Filename: "{app}\flowspace.exe"; Description: "Launch FlowSpace"; Flags: nowait postinstall skipifsilent
