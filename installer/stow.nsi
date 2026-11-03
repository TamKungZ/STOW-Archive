!include "MUI2.nsh"

!ifndef APP_ICON
!define APP_ICON "${__FILEDIR__}\..\assets\icon.ico"
!endif
!ifndef APP_NAME
!define APP_NAME "STOW Archive"
!endif
!ifndef APP_VERSION
!define APP_VERSION "0.1.0"
!endif
!ifndef APP_ARCH
!define APP_ARCH "x64"
!endif
!ifndef APP_DIR
!define APP_DIR "${__FILEDIR__}..\build\Release"
!endif
!ifndef OUTPUT_FILE
!define OUTPUT_FILE "${__FILEDIR__}..\build\STOW-Archive-Setup-${APP_VERSION}-${APP_ARCH}.exe"
!endif

Name "${APP_NAME}"
OutFile "${OUTPUT_FILE}"
Icon "${APP_ICON}"
UninstallIcon "${APP_ICON}"
InstallDir "$LOCALAPPDATA\Programs\${APP_NAME}"
InstallDirRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\STOWArchive" "InstallLocation"
RequestExecutionLevel user
Unicode true
SetCompressor /SOLID lzma

!define UNINSTALL_REG_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\STOWArchive"
!define CONTEXT_MENU_KEY "Software\Classes\*\shell\STOWArchive"
!define DIRECTORY_MENU_KEY "Software\Classes\Directory\shell\STOWArchive"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "STOW Archive" MainSection
    SetShellVarContext current
    SetOutPath "$INSTDIR"
    File "${APP_DIR}\stow.exe"
    File "${APP_DIR}\stow-cli.exe"
    File "${APP_DIR}\stow.dll"
    File "${APP_DIR}\vcruntime140.dll"

    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\STOW Archive.lnk" "$INSTDIR\stow.exe"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\STOW CLI.lnk" "$INSTDIR\stow-cli.exe"
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "Publisher" "TamKungZ"
    WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\" _?=$INSTDIR"
    WriteRegDWORD HKCU "${UNINSTALL_REG_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_REG_KEY}" "NoRepair" 1

    Call InstallContextMenu
SectionEnd

Function InstallContextMenuForKey
    Exch $R0
    WriteRegStr HKCU "$R0" "MUIVerb" "Stow"
    WriteRegStr HKCU "$R0" "SubCommands" ""
    WriteRegStr HKCU "$R0" "Icon" "$INSTDIR\stow.exe"

    WriteRegStr HKCU "$R0\shell\open" "MUIVerb" "Open side-by-side"
    WriteRegStr HKCU "$R0\shell\open\command" "" "$\"$INSTDIR\stow.exe$\" --open $\"%1$\""
    WriteRegStr HKCU "$R0\shell\compress" "MUIVerb" "Compress..."
    WriteRegStr HKCU "$R0\shell\compress\command" "" "$\"$INSTDIR\stow.exe$\" --compress $\"%1$\""
    WriteRegStr HKCU "$R0\shell\extract" "MUIVerb" "Decompress"
    WriteRegStr HKCU "$R0\shell\extract\command" "" "$\"$INSTDIR\stow.exe$\" --extract $\"%1$\""
    Pop $R0
FunctionEnd

Function InstallContextMenu
    Push "${CONTEXT_MENU_KEY}"
    Call InstallContextMenuForKey
    Push "${DIRECTORY_MENU_KEY}"
    Call InstallContextMenuForKey
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
FunctionEnd

Section "Uninstall"
    SetShellVarContext current
    DeleteRegKey HKCU "${CONTEXT_MENU_KEY}"
    DeleteRegKey HKCU "${DIRECTORY_MENU_KEY}"
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
    DeleteRegKey HKCU "${UNINSTALL_REG_KEY}"

    Delete "$SMPROGRAMS\${APP_NAME}\STOW Archive.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\STOW CLI.lnk"
    RMDir "$SMPROGRAMS\${APP_NAME}"

    Delete "$INSTDIR\stow.exe"
    Delete "$INSTDIR\stow-cli.exe"
    Delete "$INSTDIR\stow.dll"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
SectionEnd
