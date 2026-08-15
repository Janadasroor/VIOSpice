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

    ; File Associations & Windows Search Indexer Registration
    ; 1. Schematic (.flxsch and .fluxsch)
    WriteRegStr HKCU "Software\Classes\.flxsch" "" "VioraEDA.Schematic.1"
    WriteRegStr HKCU "Software\Classes\.flxsch" "Content Type" "application/x-viora-schematic"
    WriteRegStr HKCU "Software\Classes\.flxsch" "PerceivedType" "document"
    WriteRegStr HKCU "Software\Classes\.flxsch\OpenWithProgids" "VioraEDA.Schematic.1" ""
    WriteRegStr HKCU "Software\Classes\.flxsch\PersistentHandler" "" "{5e941d80-bf96-11cd-b579-08002b30bfeb}"

    WriteRegStr HKCU "Software\Classes\.fluxsch" "" "VioraEDA.Schematic.1"
    WriteRegStr HKCU "Software\Classes\.fluxsch" "Content Type" "application/x-viora-schematic"
    WriteRegStr HKCU "Software\Classes\.fluxsch" "PerceivedType" "document"
    WriteRegStr HKCU "Software\Classes\.fluxsch\OpenWithProgids" "VioraEDA.Schematic.1" ""
    WriteRegStr HKCU "Software\Classes\.fluxsch\PersistentHandler" "" "{5e941d80-bf96-11cd-b579-08002b30bfeb}"

    WriteRegStr HKCU "Software\Classes\VioraEDA.Schematic.1" "" "VioraEDA Schematic Document"
    WriteRegStr HKCU "Software\Classes\VioraEDA.Schematic.1" "FriendlyTypeName" "VioraEDA Schematic Document"
    WriteRegStr HKCU "Software\Classes\VioraEDA.Schematic.1\DefaultIcon" "" "$INSTDIR\bin\VioraEDA.exe,0"
    WriteRegStr HKCU "Software\Classes\VioraEDA.Schematic.1\shell\open\command" "" '"$INSTDIR\bin\VioraEDA.exe" "%1"'
    WriteRegStr HKCU "Software\Classes\VioraEDA.Schematic.1\shell\open" "FriendlyAppName" "VioraEDA Suite"

    ; 2. FluxScript (.flux)
    WriteRegStr HKCU "Software\Classes\.flux" "" "VioraEDA.FluxScript.1"
    WriteRegStr HKCU "Software\Classes\.flux" "Content Type" "text/plain"
    WriteRegStr HKCU "Software\Classes\.flux" "PerceivedType" "document"
    WriteRegStr HKCU "Software\Classes\.flux\OpenWithProgids" "VioraEDA.FluxScript.1" ""
    WriteRegStr HKCU "Software\Classes\.flux\PersistentHandler" "" "{5e941d80-bf96-11cd-b579-08002b30bfeb}"

    WriteRegStr HKCU "Software\Classes\VioraEDA.FluxScript.1" "" "FluxScript Source Document"
    WriteRegStr HKCU "Software\Classes\VioraEDA.FluxScript.1" "FriendlyTypeName" "FluxScript Source Document"
    WriteRegStr HKCU "Software\Classes\VioraEDA.FluxScript.1\DefaultIcon" "" "$INSTDIR\bin\VioraEDA.exe,0"
    WriteRegStr HKCU "Software\Classes\VioraEDA.FluxScript.1\shell\open\command" "" '"$INSTDIR\bin\VioraEDA.exe" "%1"'
    WriteRegStr HKCU "Software\Classes\VioraEDA.FluxScript.1\shell\open" "FriendlyAppName" "VioraEDA Suite"

    ; 3. PCB (.flxpcb)
    WriteRegStr HKCU "Software\Classes\.flxpcb" "" "VioraEDA.PCB.1"
    WriteRegStr HKCU "Software\Classes\.flxpcb" "Content Type" "application/x-viora-pcb"
    WriteRegStr HKCU "Software\Classes\.flxpcb" "PerceivedType" "document"
    WriteRegStr HKCU "Software\Classes\.flxpcb\OpenWithProgids" "VioraEDA.PCB.1" ""
    WriteRegStr HKCU "Software\Classes\.flxpcb\PersistentHandler" "" "{5e941d80-bf96-11cd-b579-08002b30bfeb}"

    WriteRegStr HKCU "Software\Classes\VioraEDA.PCB.1" "" "VioraEDA PCB Layout Document"
    WriteRegStr HKCU "Software\Classes\VioraEDA.PCB.1" "FriendlyTypeName" "VioraEDA PCB Layout Document"
    WriteRegStr HKCU "Software\Classes\VioraEDA.PCB.1\DefaultIcon" "" "$INSTDIR\bin\VioraEDA.exe,0"
    WriteRegStr HKCU "Software\Classes\VioraEDA.PCB.1\shell\open\command" "" '"$INSTDIR\bin\VioraEDA.exe" "%1"'
    WriteRegStr HKCU "Software\Classes\VioraEDA.PCB.1\shell\open" "FriendlyAppName" "VioraEDA Suite"

    ; 4. SPICE Netlist (.cir and .sp)
    WriteRegStr HKCU "Software\Classes\.cir" "" "VioraEDA.Netlist.1"
    WriteRegStr HKCU "Software\Classes\.cir" "Content Type" "text/plain"
    WriteRegStr HKCU "Software\Classes\.cir" "PerceivedType" "document"
    WriteRegStr HKCU "Software\Classes\.cir\OpenWithProgids" "VioraEDA.Netlist.1" ""
    WriteRegStr HKCU "Software\Classes\.cir\PersistentHandler" "" "{5e941d80-bf96-11cd-b579-08002b30bfeb}"

    WriteRegStr HKCU "Software\Classes\.sp" "" "VioraEDA.Netlist.1"
    WriteRegStr HKCU "Software\Classes\.sp" "Content Type" "text/plain"
    WriteRegStr HKCU "Software\Classes\.sp" "PerceivedType" "document"
    WriteRegStr HKCU "Software\Classes\.sp\OpenWithProgids" "VioraEDA.Netlist.1" ""
    WriteRegStr HKCU "Software\Classes\.sp\PersistentHandler" "" "{5e941d80-bf96-11cd-b579-08002b30bfeb}"

    WriteRegStr HKCU "Software\Classes\VioraEDA.Netlist.1" "" "SPICE Netlist Document"
    WriteRegStr HKCU "Software\Classes\VioraEDA.Netlist.1" "FriendlyTypeName" "SPICE Netlist Document"
    WriteRegStr HKCU "Software\Classes\VioraEDA.Netlist.1\DefaultIcon" "" "$INSTDIR\bin\VioraEDA.exe,0"
    WriteRegStr HKCU "Software\Classes\VioraEDA.Netlist.1\shell\open\command" "" '"$INSTDIR\bin\VioraEDA.exe" "%1"'
    WriteRegStr HKCU "Software\Classes\VioraEDA.Netlist.1\shell\open" "FriendlyAppName" "VioraEDA Suite"

    ; 5. Application OpenWith & App Paths
    WriteRegStr HKCU "Software\Classes\Applications\VioraEDA.exe" "FriendlyAppName" "VioraEDA Suite"
    WriteRegStr HKCU "Software\Classes\Applications\VioraEDA.exe\shell\open\command" "" '"$INSTDIR\bin\VioraEDA.exe" "%1"'
    WriteRegStr HKCU "Software\Classes\Applications\VioraEDA.exe\SupportedTypes" ".flxsch" ""
    WriteRegStr HKCU "Software\Classes\Applications\VioraEDA.exe\SupportedTypes" ".fluxsch" ""
    WriteRegStr HKCU "Software\Classes\Applications\VioraEDA.exe\SupportedTypes" ".flux" ""
    WriteRegStr HKCU "Software\Classes\Applications\VioraEDA.exe\SupportedTypes" ".flxpcb" ""
    WriteRegStr HKCU "Software\Classes\Applications\VioraEDA.exe\SupportedTypes" ".cir" ""
    WriteRegStr HKCU "Software\Classes\Applications\VioraEDA.exe\SupportedTypes" ".sp" ""

    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\VioraEDA.exe" "" "$INSTDIR\bin\VioraEDA.exe"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\VioraEDA.exe" "Path" "$INSTDIR\bin"

    ; Notify Windows Explorer & Start Menu to reload file associations immediately
    System::Call 'shell32.dll::SHChangeNotify(i, i, i, i) v (0x08000000, 0, 0, 0)'
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
    DeleteRegKey HKCU "Software\Classes\.fluxsch"
    DeleteRegKey HKCU "Software\Classes\VioraEDA.FluxScript.1"
    DeleteRegKey HKCU "Software\Classes\.flux"
    DeleteRegKey HKCU "Software\Classes\VioraEDA.PCB.1"
    DeleteRegKey HKCU "Software\Classes\.flxpcb"
    DeleteRegKey HKCU "Software\Classes\VioraEDA.Netlist.1"
    DeleteRegKey HKCU "Software\Classes\.cir"
    DeleteRegKey HKCU "Software\Classes\.sp"
    DeleteRegKey HKCU "Software\Classes\Applications\VioraEDA.exe"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\VioraEDA.exe"

    ; Remove Uninstall and App Registry
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VioraEDA"
    DeleteRegKey HKCU "Software\VioraEDA"

    ; Notify Windows Explorer & Start Menu to reload file associations
    System::Call 'shell32.dll::SHChangeNotify(i, i, i, i) v (0x08000000, 0, 0, 0)'

    ; Remove Installed Files
    RMDir /r "$INSTDIR\bin"
    RMDir /r "$INSTDIR\cm"
    RMDir /r "$INSTDIR\templates"
    RMDir /r "$INSTDIR\ViospiceLib"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
SectionEnd
