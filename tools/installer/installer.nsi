!include "MUI2.nsh"
!include "FileFunc.nsh"

; General Configuration
Name "VioraEDA Suite"

!ifndef OUTFILE
!define OUTFILE "..\..\VioraEDA-v0.2.0-beta-windows-x86_64-Setup.exe"
!endif
OutFile "${OUTFILE}"

!ifndef VERSION
!define VERSION "0.2.0-beta"
!endif

InstallDir "$LOCALAPPDATA\Programs\VioraEDA"
InstallDirRegKey HKCU "Software\VioraEDA" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma

; Interface Configuration
!define MUI_ABORTWARNING
!define MUI_ICON "..\..\resources\viora_eda_logo.ico"
!define MUI_UNICON "..\..\resources\viora_eda_logo.ico"
!define MUI_HEADERIMAGE
!define MUI_WELCOMEFINISHPAGE_BITMAP_NOSTRETCH

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\VioraEDA.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch VioraEDA Suite"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Languages
!insertmacro MUI_LANGUAGE "English"

; Version Info
VIProductVersion "0.2.0.0"
VIAddVersionKey "ProductName" "VioraEDA Suite"
VIAddVersionKey "CompanyName" "Janadasroor Team"
VIAddVersionKey "LegalCopyright" "Copyright 2026 Janadasroor Team. Apache 2.0"
VIAddVersionKey "FileDescription" "VioraEDA Suite Installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"

Section "MainSection" SEC01
    SetOutPath "$INSTDIR"
    File /r "..\..\staging\*"

    ; Create Uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Registry Keys for Application
    WriteRegStr HKCU "Software\VioraEDA" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "Software\VioraEDA" "Version" "${VERSION}"

    ; Windows Add/Remove Programs Registration
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VioraEDA" "DisplayName" "VioraEDA Suite (${VERSION})"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VioraEDA" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VioraEDA" "Publisher" "Janadasroor Team"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VioraEDA" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VioraEDA" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VioraEDA" "DisplayIcon" "$INSTDIR\bin\VioraEDA.exe,0"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VioraEDA" "InstallLocation" "$INSTDIR"
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VioraEDA" "NoModify" 1
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VioraEDA" "NoRepair" 1

    ; Estimate Installed Size
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VioraEDA" "EstimatedSize" "$0"

    ; Start Menu Shortcuts
    CreateDirectory "$SMPROGRAMS\VioraEDA"
    CreateShortcut "$SMPROGRAMS\VioraEDA\VioraEDA.lnk" "$INSTDIR\bin\VioraEDA.exe" "" "$INSTDIR\bin\VioraEDA.exe" 0
    CreateShortcut "$SMPROGRAMS\VioraEDA\Viora CLI.lnk" "$INSTDIR\bin\viora.exe" "" "$INSTDIR\bin\viora.exe" 0
    CreateShortcut "$SMPROGRAMS\VioraEDA\Uninstall VioraEDA.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\Uninstall.exe" 0

    ; Desktop Shortcut
    CreateShortcut "$DESKTOP\VioraEDA.lnk" "$INSTDIR\bin\VioraEDA.exe" "" "$INSTDIR\bin\VioraEDA.exe" 0

    ; File Associations
    WriteRegStr HKCU "Software\Classes\.flxsch" "" "VioraEDA.Schematic.1"
    WriteRegStr HKCU "Software\Classes\VioraEDA.Schematic.1" "" "VioraEDA Schematic Document"
    WriteRegStr HKCU "Software\Classes\VioraEDA.Schematic.1\DefaultIcon" "" "$INSTDIR\bin\VioraEDA.exe,0"
    WriteRegStr HKCU "Software\Classes\VioraEDA.Schematic.1\shell\open\command" "" '"$INSTDIR\bin\VioraEDA.exe" "%1"'

    WriteRegStr HKCU "Software\Classes\.flux" "" "VioraEDA.FluxScript.1"
    WriteRegStr HKCU "Software\Classes\VioraEDA.FluxScript.1" "" "FluxScript Source Document"
    WriteRegStr HKCU "Software\Classes\VioraEDA.FluxScript.1\DefaultIcon" "" "$INSTDIR\bin\VioraEDA.exe,0"
    WriteRegStr HKCU "Software\Classes\VioraEDA.FluxScript.1\shell\open\command" "" '"$INSTDIR\bin\VioraEDA.exe" "%1"'

    WriteRegStr HKCU "Software\Classes\.flxpcb" "" "VioraEDA.PCB.1"
    WriteRegStr HKCU "Software\Classes\VioraEDA.PCB.1" "" "VioraEDA PCB Layout Document"
    WriteRegStr HKCU "Software\Classes\VioraEDA.PCB.1\DefaultIcon" "" "$INSTDIR\bin\VioraEDA.exe,0"
    WriteRegStr HKCU "Software\Classes\VioraEDA.PCB.1\shell\open\command" "" '"$INSTDIR\bin\VioraEDA.exe" "%1"'
SectionEnd

Section "Uninstall"
    ; Remove Desktop & Start Menu Shortcuts
    Delete "$DESKTOP\VioraEDA.lnk"
    Delete "$SMPROGRAMS\VioraEDA\VioraEDA.lnk"
    Delete "$SMPROGRAMS\VioraEDA\Viora CLI.lnk"
    Delete "$SMPROGRAMS\VioraEDA\Uninstall VioraEDA.lnk"
    RMDir "$SMPROGRAMS\VioraEDA"

    ; Remove File Associations
    DeleteRegKey HKCU "Software\Classes\VioraEDA.Schematic.1"
    DeleteRegKey HKCU "Software\Classes\.flxsch"
    DeleteRegKey HKCU "Software\Classes\VioraEDA.FluxScript.1"
    DeleteRegKey HKCU "Software\Classes\.flux"
    DeleteRegKey HKCU "Software\Classes\VioraEDA.PCB.1"
    DeleteRegKey HKCU "Software\Classes\.flxpcb"

    ; Remove Uninstall and App Registry
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VioraEDA"
    DeleteRegKey HKCU "Software\VioraEDA"

    ; Remove Installed Files
    RMDir /r "$INSTDIR\bin"
    RMDir /r "$INSTDIR\cm"
    RMDir /r "$INSTDIR\templates"
    RMDir /r "$INSTDIR\ViospiceLib"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
SectionEnd
