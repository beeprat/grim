; Inno Setup script for grim. (Inno Setup 6). Packs dist\GRIM, made as described in CLAUDE.md
; ("Distribution package"). Build: "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\grim.iss
; Output: dist\grim-setup.exe

#define AppName "grim."
#define AppVersion "0.1.0"
#define AppPublisher "šarić"
#define AppExe "GRIM.exe"

[Setup]
; Never change AppId: it is how updates and the uninstaller recognise the installed app.
AppId={{67E6D47A-0ED7-4BF8-85D8-8B4B6B03ACFE}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\grim
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Installs for the current user without admin rights; the user can choose "all users" instead.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=..\dist
OutputBaseFilename=grim-setup
SetupIconFile=..\resources\app.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
Compression=lzma2/ultra64
SolidCompression=yes
; Minimal look: dark like the app, no welcome/ready pages, folder page only on the "all users" choice
; or an update path change, white star instead of the stock wizard pictures.
WizardStyle=modern dark hidebevels
WizardSizePercent=100
WizardImageFile=
WizardSmallImageFile=star-58.png,star-116.png
WizardImageBackColor=#141414
WizardBackColor=#141414
DisableWelcomePage=yes
DisableDirPage=auto
DisableReadyPage=yes
DisableFinishedPage=no
ShowLanguageDialog=no
; Closes a running grim. before replacing its files.
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\dist\GRIM\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

; Settings, ratings, playlists and statistics (%APPDATA%\grimPlayer) and caches (%LOCALAPPDATA%\grimPlayer)
; are left in place on uninstall, so reinstalling keeps them.
