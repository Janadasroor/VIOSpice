# Packaging rules for VioraEDA (CPack NSIS on Windows).
# Layout mirrors the release .zip staging used by .github/workflows/release.yml:
#   bin/           executables + Qt/MinGW/engine DLLs (windeployqt + install(CODE))
#   cm/            ngspice code models
#   python/        templates/automation/scripts
#   examples/      example .flux/.flxsch files
#   templates/     smart-block circuit templates
#   core/simulation/model_params/   BSIM3/4 model parameter JSON files
#   ViospiceLib/   bundled symbol/model/footprint library (moved to user home on install)

# ---------------------------------------------------------------------------
# Executables
# ---------------------------------------------------------------------------
install(TARGETS VioraEDA viora flux_runner viospice-merge
    RUNTIME DESTINATION bin
)

if(TARGET flux-lsp)
    install(TARGETS flux-lsp RUNTIME DESTINATION bin)
endif()

# ---------------------------------------------------------------------------
# Runtime data
# ---------------------------------------------------------------------------
install(DIRECTORY "${CMAKE_BINARY_DIR}/cm" DESTINATION "." FILES_MATCHING PATTERN "*.cm")

install(DIRECTORY "${CMAKE_SOURCE_DIR}/python/templates"
    DESTINATION python
    FILES_MATCHING PATTERN "*.flux")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/python/automation"
    DESTINATION python
    FILES_MATCHING PATTERN "*.flux")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/python/scripts"
    DESTINATION python
    FILES_MATCHING PATTERN "*.py")

install(DIRECTORY "${CMAKE_SOURCE_DIR}/examples" DESTINATION "." FILES_MATCHING PATTERN "*")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/templates" DESTINATION ".")

install(DIRECTORY "${CMAKE_SOURCE_DIR}/core/simulation/model_params"
    DESTINATION core/simulation
    FILES_MATCHING PATTERN "*.json")

# ---------------------------------------------------------------------------
# Bundled ViospiceLib library
# ---------------------------------------------------------------------------
set(VIOSPICE_BUNDLED_LIB "" CACHE PATH
    "Directory to bundle as ViospiceLib in the installer (defaults to ~/ViospiceLib)")
if(VIOSPICE_BUNDLED_LIB STREQUAL "")
    if(WIN32 AND DEFINED ENV{USERPROFILE} AND EXISTS "$ENV{USERPROFILE}/ViospiceLib")
        set(VIOSPICE_BUNDLED_LIB "$ENV{USERPROFILE}/ViospiceLib")
    elseif(DEFINED ENV{HOME} AND EXISTS "$ENV{HOME}/ViospiceLib")
        set(VIOSPICE_BUNDLED_LIB "$ENV{HOME}/ViospiceLib")
    elseif(EXISTS "C:/Users/$ENV{USERNAME}/ViospiceLib")
        set(VIOSPICE_BUNDLED_LIB "C:/Users/$ENV{USERNAME}/ViospiceLib")
    endif()
endif()

if(EXISTS "${VIOSPICE_BUNDLED_LIB}" AND IS_DIRECTORY "${VIOSPICE_BUNDLED_LIB}")
    message(STATUS "Packaging ViospiceLib from ${VIOSPICE_BUNDLED_LIB}")
    install(DIRECTORY "${VIOSPICE_BUNDLED_LIB}" DESTINATION "."
        PATTERN ".git" EXCLUDE
        PATTERN "sym_backup*" EXCLUDE
    )
else()
    message(WARNING "ViospiceLib not found; installer will ship without the bundled library")
endif()

# ---------------------------------------------------------------------------
# Qt / runtime DLLs (windeployqt at install time)
# ---------------------------------------------------------------------------
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt)
    if(WINDEPLOYQT_EXECUTABLE)
        set(VIO_DEPLOY_TARGETS "VioraEDA.exe viora.exe")
        if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/flux_runner.exe")
            string(APPEND VIO_DEPLOY_TARGETS " flux_runner.exe")
        endif()
        string(REPLACE " " ";" VIO_DEPLOY_TARGET_LIST "${VIO_DEPLOY_TARGETS}")
        foreach(_tgt IN LISTS VIO_DEPLOY_TARGET_LIST)
            install(CODE "
                execute_process(COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                    --dir \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin\"
                    \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/${_tgt}\"
                    RESULT_VARIABLE _wdeploy_rc
                    OUTPUT_VARIABLE _wdeploy_out
                    ERROR_VARIABLE _wdeploy_out)
                if(NOT _wdeploy_rc EQUAL 0)
                    message(WARNING \"windeployqt ${_tgt} failed: \${_wdeploy_out}\")
                endif()
            ")
        endforeach()
    else()
        message(WARNING "windeployqt not found; Qt DLLs will NOT be bundled")
    endif()
endif()

# ---------------------------------------------------------------------------
# CPack / NSIS configuration
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "VioraEDA")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Janadasroor Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "High-performance open-source EDA/SPICE simulator")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Janadasroor/VioraEDA")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_PACKAGE_FILE_NAME "VioraEDA-${PROJECT_VERSION}-windows-x86_64")
    set(CPACK_NSIS_PACKAGE_NAME "VioraEDA ${PROJECT_VERSION}")
    set(CPACK_NSIS_DISPLAY_NAME "VioraEDA ${PROJECT_VERSION}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "VioraEDA")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    # The app's viora_eda_logo.ico uses PNG compression, which NSIS cannot
    # embed (it silently falls back to the default icon), so the installer
    # uses a classic BMP-based ICO derived from the same logo.
    set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/resources/installer/viora_eda_logo.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/resources/installer/viora_eda_logo.ico")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)

    # Installer theme: dark slate + accent (matches the app's visuals).
    set(CPACK_NSIS_MUI_HEADERIMAGE "${CMAKE_SOURCE_DIR}/resources/installer/installer_header.bmp")
    set(CPACK_NSIS_MUI_WELCOMEFINISHPAGE_BITMAP "${CMAKE_SOURCE_DIR}/resources/installer/installer_side.bmp")
    set(CPACK_NSIS_BRANDING_TEXT "VioraEDA")
    set(CPACK_NSIS_WELCOME_TITLE "Welcome to VioraEDA ${PROJECT_VERSION} Setup")
    set(CPACK_NSIS_FINISH_TITLE "VioraEDA ${PROJECT_VERSION} installation complete")
    # Overrides the generator's default MUI_ICON/MUI_UNICON defines, so both
    # must be re-declared here alongside the color scheme.
    set(CPACK_NSIS_INSTALLER_MUI_ICON_CODE "
!define MUI_ICON ${CMAKE_SOURCE_DIR}/resources/installer/viora_eda_logo.ico
!define MUI_UNICON ${CMAKE_SOURCE_DIR}/resources/installer/viora_eda_logo.ico
!define MUI_BGCOLOR 141417
!define MUI_TEXTCOLOR F4F4F5
")
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_UNINSTALL_NAME "Uninstall VioraEDA")
    set(CPACK_NSIS_MENU_LINKS
        "bin/viora.exe" "VioraEDA CLI"
    )
    set(CPACK_CREATE_DESKTOP_LINKS "VioraEDA")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")

    # Point NSIS' default shortcut (and the desktop link) at the real exe path.
    set(CPACK_PACKAGE_EXECUTABLES "VioraEDA" "VioraEDA")

    # On install, move the bundled library into the user's home directory if they
    # don't already have one. Kept out of $INSTDIR afterwards so the uninstaller
    # leaves the user's library intact. ($USERPROFILE is used rather than $PROFILE
    # because the NSIS script runs under SetShellVarContext all, where $PROFILE
    # would resolve to the All-Users profile instead of the current user's home.)
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
        IfFileExists \\\"\$USERPROFILE\\\\ViospiceLib\\\" VioraLibPresent
          Rename \\\"\$INSTDIR\\\\ViospiceLib\\\" \\\"\$USERPROFILE\\\\ViospiceLib\\\"
        VioraLibPresent:
    ")

    # File associations (open with VioraEDA).
    set(CPACK_NSIS_EXTRA_REGISTRY_COMMANDS "
        WriteRegStr HKCR \\\".flxsch\\\" \\\"\\\" \\\"VioraEDA.Schematic\\\"
        WriteRegStr HKCR \\\"VioraEDA.Schematic\\\\DefaultIcon\\\" \\\"\\\" \\\"\\\$INSTDIR\\\\bin\\\\VioraEDA.exe,0\\\"
        WriteRegStr HKCR \\\"VioraEDA.Schematic\\\\shell\\\\open\\\\command\\\" \\\"\\\" \\\"\\\"\\\"\\\$INSTDIR\\\\bin\\\\VioraEDA.exe\\\"\\\" \\\"\\\"%1\\\"\\\"\\\"
        WriteRegStr HKCR \\\".pcb\\\" \\\"\\\" \\\"VioraEDA.PCB\\\"
        WriteRegStr HKCR \\\"VioraEDA.PCB\\\\DefaultIcon\\\" \\\"\\\" \\\"\\\$INSTDIR\\\\bin\\\\VioraEDA.exe,0\\\"
        WriteRegStr HKCR \\\"VioraEDA.PCB\\\\shell\\\\open\\\\command\\\" \\\"\\\" \\\"\\\"\\\"\\\$INSTDIR\\\\bin\\\\VioraEDA.exe\\\"\\\" \\\"\\\"%1\\\"\\\"\\\"
        WriteRegStr HKCR \\\".flux\\\" \\\"\\\" \\\"VioraEDA.Flux\\\"
        WriteRegStr HKCR \\\"VioraEDA.Flux\\\\shell\\\\open\\\\command\\\" \\\"\\\" \\\"\\\"\\\"\\\$INSTDIR\\\\bin\\\\VioraEDA.exe\\\"\\\" \\\"\\\"%1\\\"\\\"\\\"
    ")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
        DeleteRegKey HKCR VioraEDA.Schematic
        DeleteRegKey HKCR VioraEDA.PCB
        DeleteRegKey HKCR VioraEDA.Flux
        DeleteRegKey HKCR \\\".flxsch\\\"
        DeleteRegKey HKCR \\\".pcb\\\"
        DeleteRegKey HKCR \\\".flux\\\"
    ")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;TGZ")
else()
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_DEBIAN_PACKAGE_SECTION "electronics")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6widgets6 (>= 6.10), python3 (>= 3.10)")
endif()

include(CPack)
