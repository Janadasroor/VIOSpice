# FindVIOSPICE_ENGINE.cmake
# Find the ngspice shared library and headers

set(VIOSPICE_ENGINE_ROOT "" CACHE PATH "Path to VioMATRIXC root")

# Check for library
find_library(VIOSPICE_ENGINE_LIBRARY 
    NAMES ngspice libngspice.so libngspice.dll ngspice.dll libngspice.dylib
    PATHS 
        "${VIOSPICE_ENGINE_ROOT}/releasesh/src/.libs"
        "${VIOSPICE_ENGINE_ROOT}/release/src/.libs"
        "${VIOSPICE_ENGINE_ROOT}/src/.libs"
        /usr/local/lib /usr/lib /usr/lib64
    DOC "Path to ngspice shared library"
)

# Check for include directory
find_path(VIOSPICE_ENGINE_INCLUDE_DIR 
    NAMES ngspice/sharedspice.h
    PATHS 
        "${VIOSPICE_ENGINE_ROOT}/src/include"
        /usr/local/include /usr/include
    DOC "Path to ngspice headers"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(VIOSPICE_ENGINE DEFAULT_MSG VIOSPICE_ENGINE_LIBRARY VIOSPICE_ENGINE_INCLUDE_DIR)

if(VIOSPICE_ENGINE_FOUND)
    set(VIOSPICE_ENGINE_LIBRARIES ${VIOSPICE_ENGINE_LIBRARY})
    set(VIOSPICE_ENGINE_INCLUDE_DIRS ${VIOSPICE_ENGINE_INCLUDE_DIR})
    
    message(STATUS "NGSPICE engine found!")
    message(STATUS "  Library: ${VIOSPICE_ENGINE_LIBRARIES}")
    message(STATUS "  Include: ${VIOSPICE_ENGINE_INCLUDE_DIRS}")
    
    if(NOT TARGET VIOSPICE_ENGINE::viospice)
        add_library(VIOSPICE_ENGINE::viospice UNKNOWN IMPORTED)
        set_target_properties(VIOSPICE_ENGINE::viospice PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${VIOSPICE_ENGINE_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${VIOSPICE_ENGINE_LIBRARIES}"
        )
    endif()
endif()

mark_as_advanced(VIOSPICE_ENGINE_INCLUDE_DIR VIOSPICE_ENGINE_LIBRARY)
