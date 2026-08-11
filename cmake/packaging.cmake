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
install(TARGETS VioraEDA VioraEDA_Setup viora flux_runner viospice-merge
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

    # Bundle C++ / MinGW runtime, Python, libcurl, multimedia codecs, and simulation engine DLLs into bin/
    file(GLOB VIO_RUNTIME_DLLS
        "C:/msys64/mingw64/bin/*.dll"
        "C:/Python314/python314.dll"
        "C:/Python314/libpython3.14.dll"
        "${CMAKE_BINARY_DIR}/libngspice-0.dll"
        "${CMAKE_BINARY_DIR}/_deps/viomatrixc-src/src/.libs/libngspice-0.dll"
        "${CMAKE_BINARY_DIR}/vioavr-prebuilt/lib/avr_cosim.dll"
    )
    install(FILES ${VIO_RUNTIME_DLLS} DESTINATION bin OPTIONAL)
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
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_FILE_NAME "VioraEDA-${PROJECT_VERSION}-windows-x86_64")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "VioraEDA")

elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;TGZ")
else()
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_DEBIAN_PACKAGE_SECTION "electronics")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6widgets6 (>= 6.10), python3 (>= 3.10)")
endif()

include(CPack)
