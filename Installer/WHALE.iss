; Windows installer for WHALE (Inno Setup 6). Built by .github/workflows/build.yml.
#ifndef AppVersion
  #define AppVersion "1.1.0-beta2"
#endif

[Setup]
AppId={{9B6F3E21-7A4C-4E58-8D2B-5C1E0A7F4E11}
AppName=WHALE Mastering
AppVersion={#AppVersion}
VersionInfoVersion=1.1.0.0
AppPublisher=Whale Audio
DefaultDirName={commoncf64}\VST3
DirExistsWarning=no
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputBaseFilename=WHALE-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName=WHALE Mastering (VST3)
WizardStyle=modern

[Files]
Source: "..\build\WHALE_artefacts\Release\VST3\WHALE.vst3\*"; DestDir: "{commoncf64}\VST3\WHALE.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[Messages]
FinishedLabel=WHALE est installé dans C:\Program Files\Common Files\VST3.%n%nFL Studio : Options > Manage plugins > Find more plugins.%nAbleton Live : Préférences > Plug-ins > Rescan.%nMets WHALE en dernier sur le bus Master.
