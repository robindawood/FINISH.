; Windows installer for FINISH (Inno Setup 6). Built by .github/workflows/build.yml.
#ifndef AppVersion
  #define AppVersion "1.2.0"
#endif

[Setup]
AppId={{6E1B0C2A-4F2D-4C6B-9B1E-3A7D2C9F0001}
AppName=FINISH
AppVersion={#AppVersion}
AppPublisher=FINISH Audio
DefaultDirName={commoncf64}\VST3
DirExistsWarning=no
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputBaseFilename=FINISH-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName=FINISH (VST3)
WizardStyle=modern

[Files]
Source: "..\build\FINISH_artefacts\Release\VST3\FINISH.vst3\*"; DestDir: "{commoncf64}\VST3\FINISH.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[Messages]
FinishedLabel=FINISH is installed in C:\Program Files\Common Files\VST3.%n%nFL Studio: Options > Manage plugins > Find more plugins.%nAbleton Live: Preferences > Plug-ins > Rescan.
