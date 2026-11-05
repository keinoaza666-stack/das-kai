; SPDX-License-Identifier: AGPL-3.0-only
; DAS Kai Windows installer (Inno Setup 6). Adapted from upstream
; DawAudioStreamer's installer/DawAudioStreamer.iss (2026-11-05): DAS Kai
; names, its own AppId so it installs next to the original, no Discord or
; virtual-audio parts, and the files come from one staged folder.
; Built by cmake/CreateWindowsInstaller.cmake (target das_kai_package), which
; stages the files and passes AppVersion, StageDir and OutputDir.
; Saved as UTF-8 with a BOM so Inno Setup reads the Japanese text.
#ifndef AppVersion
  #error AppVersion is required (run the das_kai_package target)
#endif
#ifndef StageDir
  #error StageDir is required (run the das_kai_package target)
#endif
#define MyAppName "DAS Kai"
#define MyAppPublisher "Keinoaza"
#define MyAppUrl "https://github.com/keinoaza666-stack/das-kai"
#define ReadmeName "README_はじめにお読みください.txt"

[Setup]
; Never reuse upstream's AppId: the two must uninstall independently.
AppId={{230D027E-4530-4AC3-900E-DDAEAEC78284}
AppName={#MyAppName}
AppVersion={#AppVersion}
AppVerName={#MyAppName} {#AppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppUrl}
AppSupportURL={#MyAppUrl}
AppUpdatesURL={#MyAppUrl}/releases
AppComments={cm:AppComments}
VersionInfoVersion={#AppVersion}.0
VersionInfoProductVersion={#AppVersion}.0
VersionInfoDescription=DAS Kai Setup
VersionInfoCompany={#MyAppPublisher}
VersionInfoCopyright=Copyright (c) 2026 yoruhinot, Keinoaza
DefaultDirName={autopf}\Keinoaza\DAS Kai
DisableDirPage=yes
DefaultGroupName=DAS Kai
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=DAS-Kai-{#AppVersion}-Windows-x64-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; The VST3 goes to Common Files and the OBS plug-in to ProgramData, as
; upstream does; both need administrator rights.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
UninstallDisplayName={#MyAppName} {#AppVersion}
UninstallDisplayIcon={uninstallexe}
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
LanguageDetectionMethod=uilanguage
ShowLanguageDialog=auto

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
japanese.AppComments=DAW の音を、仮想オーディオデバイスなしで OBS に送ります
japanese.IconReadme=はじめにお読みください
japanese.IconUninstall=DAS Kai のアンインストール
japanese.RunReadme=「はじめにお読みください」を開く
english.AppComments=Sends your DAW's audio to OBS without a virtual audio device
english.IconReadme=Read me first
english.IconUninstall=Uninstall DAS Kai
english.RunReadme=Open "Read me first"

[InstallDelete]
; An update replaces the whole bundle, so no file of an older version stays.
Type: filesandordirs; Name: "{commoncf64}\VST3\DAS Kai.vst3"
Type: filesandordirs; Name: "{commonappdata}\obs-studio\plugins\das-kai-obs"

[Files]
Source: "{#StageDir}\DAS Kai.vst3\*"; DestDir: "{commoncf64}\VST3\DAS Kai.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs restartreplace uninsrestartdelete
Source: "{#StageDir}\obs\das-kai-obs\*"; DestDir: "{commonappdata}\obs-studio\plugins\das-kai-obs"; Flags: ignoreversion recursesubdirs createallsubdirs restartreplace uninsrestartdelete
Source: "{#StageDir}\docs\*"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{cm:IconReadme}"; Filename: "{app}\{#ReadmeName}"
Name: "{group}\{cm:IconUninstall}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\{#ReadmeName}"; Description: "{cm:RunReadme}"; Flags: postinstall shellexec skipifsilent nowait
