# Locate the Cypress API for 
#
# This module defines the following variables:
#   CYAPI_FOUND         TRUE if the Cypress API was found
#   CYAPI_HEADER_FILE   The location of the API header
#   CYAPI_INCLUDE_DIRS  The location of header files
#   CYAPI_LIBRARIES     The Cypress library files
#
if(DEFINED __INCLUDED_BLADERF_FIND_CYAPI_CMAKE)
    return()
endif()
set(__INCLUDED_BLADERF_FIND_CYAPI_CMAKE TRUE)

set(FX3_SDK_PATH
    "C:/Program Files (x86)/Cypress/EZ-USB FX3 SDK/1.3/"
    CACHE
    PATH
    "Path to the Cypress FX3 SDK"
)

if(NOT EXISTS "${FX3_SDK_PATH}")
    message(STATUS
            "The following location does not exist: FX3_SDK_PATH=${FX3_SDK_PATH}")
    return()
endif()

find_file(CYAPI_HEADER_FILE
    NAMES
        CyAPI.h
    PATHS
        "${FX3_SDK_PATH}/library/cpp"
    PATH_SUFFIXES
        include inc
)
mark_as_advanced(CYAPI_HEADER_FILE)
get_filename_component(CYAPI_INCLUDE_DIRS "${CYAPI_HEADER_FILE}" PATH)

if(WIN32)
    if(MSVC)
        if(CMAKE_CL_64)
            set(CYAPI_LIBRARY_PATH_SUFFIX x64)
        else(CMAKE_CL_64)
            set(CYAPI_LIBRARY_PATH_SUFFIX x86)
        endif(CMAKE_CL_64)
    elseif(CMAKE_COMPILER_IS_GNUCC)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(CYAPI_LIBRARY_PATH_SUFFIX x64)
        else(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(CYAPI_LIBRARY_PATH_SUFFIX x86)
        endif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    endif()

    find_library(CYAPI_LIBRARY
        NAMES
            CyAPI 
        PATHS
            "${FX3_SDK_PATH}/library/cpp"
        PATH_SUFFIXES
            lib/${CYAPI_LIBRARY_PATH_SUFFIX}
    )
    mark_as_advanced(CYAPI_LIBRARY)
    if(CYAPI_LIBRARY)
        set(CYAPI_LIBRARIES "${CYAPI_LIBRARY}" SetupAPI.lib)
    endif()
else()
    message(FATAL_ERROR "This file only supports Windows")
endif()

if(CYAPI_INCLUDE_DIRS AND CYAPI_LIBRARIES)
    set(CYAPI_FOUND TRUE)
endif()

if(CYAPI_FOUND)
    set(CMAKE_REQUIRED_INCLUDES "${CYAPI_INCLUDE_DIRS}")
    check_include_file("${CYAPI_HEADER_FILE}" CYAPI_FOUND)
    message(STATUS "CyAPI includes: ${CYAPI_INCLUDE_DIRS}")
    message(STATUS "CyAPI libs: ${CYAPI_LIBRARIES}")
endif()

if(NOT CYAPI_FOUND AND REQUIRED)
    message(FATAL_ERROR "Cypress API not found. Double-check your FX3_SDK_PATH value.")
endif()
