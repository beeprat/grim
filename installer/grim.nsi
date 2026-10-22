; NSIS installer for grim. Packs dist\GRIM, made as described in CLAUDE.md ("Distribution package").
; Build: "C:\Program Files (x86)\NSIS\makensis.exe" installer\grim.nsi
; Output: dist\grim-setup.exe
; Minimal on purpose: no welcome/licence/folder pages, per-user install without admin rights,
; then a finish page with "Run grim." and "Desktop shortcut".

Unicode true
!include "MUI2.nsh"

!define AppName "grim."
!define AppVersion "0.1.3"
!define AppPublisher "šarić"
!define AppExe "GRIM.exe"
; Same key as the Inno Setup AppId, so Windows Settings shows one entry.
!define UninstallKey "Software\Microsoft\Windows\CurrentVersion\Uninstall\{67E6D47A-0ED7-4BF8-85D8-8B4B6B03ACFE}_is1"

Name "${AppName}"
OutFile "..\dist\grim-setup.exe"
InstallDir "$LOCALAPPDATA\Programs\grim"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 64
BrandingText " "
ShowInstDetails nevershow
ShowUninstDetails nevershow

!define MUI_ICON "..\resources\app.ico"
!define MUI_UNICON "..\resources\app.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_NOAUTOCLOSE
!define MUI_FINISHPAGE_RUN "$INSTDIR\${AppExe}"
!define MUI_FINISHPAGE_RUN_TEXT "Run ${AppName}"
; The "readme" checkbox is reused for the optional desktop shortcut.
!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Desktop shortcut"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION CreateDesktopShortcut

!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

VIProductVersion "${AppVersion}.0"
VIAddVersionKey "ProductName" "${AppName}"
VIAddVersionKey "FileDescription" "${AppName} Setup"
VIAddVersionKey "FileVersion" "${AppVersion}"
VIAddVersionKey "CompanyName" "${AppPublisher}"
VIAddVersionKey "LegalCopyright" "${AppPublisher}"

Function CreateDesktopShortcut
    CreateShortcut "$DESKTOP\${AppName}.lnk" "$INSTDIR\${AppExe}"
FunctionEnd

; Started by grim.'s update note with /S: start the new version once it is in place.
Function .onInstSuccess
    IfSilent 0 +2
    Exec '"$INSTDIR\${AppExe}"'
FunctionEnd

Section
    ; grim. quits right after starting an update: wait (up to 10 s) until its exe can be removed.
    StrCpy $0 0
    waitForExit:
    IfFileExists "$INSTDIR\${AppExe}" 0 exited
    ClearErrors
    Delete "$INSTDIR\${AppExe}"
    IfErrors 0 exited
    IntOp $0 $0 + 1
    IntCmp $0 40 exited
    Sleep 250
    Goto waitForExit
    exited:
    ; An update replaces the whole folder, so files a newer version no longer ships do not linger.
    RMDir /r "$INSTDIR"
    SetOutPath "$INSTDIR"
    File /r "..\dist\GRIM\*.*"
    WriteUninstaller "$INSTDIR\uninstall.exe"
    CreateShortcut "$SMPROGRAMS\${AppName}.lnk" "$INSTDIR\${AppExe}"

    WriteRegStr HKCU "${UninstallKey}" "DisplayName" "${AppName}"
    WriteRegStr HKCU "${UninstallKey}" "DisplayVersion" "${AppVersion}"
    WriteRegStr HKCU "${UninstallKey}" "Publisher" "${AppPublisher}"
    WriteRegStr HKCU "${UninstallKey}" "DisplayIcon" "$INSTDIR\${AppExe}"
    WriteRegStr HKCU "${UninstallKey}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UninstallKey}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegDWORD HKCU "${UninstallKey}" "NoModify" 1
    WriteRegDWORD HKCU "${UninstallKey}" "NoRepair" 1
SectionEnd

; Settings, ratings, playlists and statistics (%APPDATA%\grimPlayer) and caches (%LOCALAPPDATA%\grimPlayer)
; are left in place, so reinstalling keeps them.
Section "Uninstall"
    Delete "$SMPROGRAMS\${AppName}.lnk"
    Delete "$DESKTOP\${AppName}.lnk"
    RMDir /r "$INSTDIR"
    DeleteRegKey HKCU "${UninstallKey}"
SectionEnd
