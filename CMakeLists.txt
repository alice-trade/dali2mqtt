cmake_minimum_required(VERSION 3.20)
project(
        daliMQTT
        VERSION 0.14.0
        DESCRIPTION "DALI-to-MQTT bridge for ESP platform"
        LANGUAGES C CXX)

include(scripts/common.cmake)
set(PROJVER ${CMAKE_PROJECT_VERSION})
set(PROJDIR ${CMAKE_SOURCE_DIR})
set(PROJBRANCH "default")

if(GIT_FOUND)
    execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_BRANCH_OUTPUT
            RESULT_VARIABLE GIT_BRANCH_EXIT_CODE
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
    )
    if(GIT_BRANCH_EXIT_CODE EQUAL 0 AND GIT_BRANCH_OUTPUT)
        set(PROJBRANCH ${GIT_BRANCH_OUTPUT})
    endif()
endif()

if(NOT DEFINED TARGET AND DEFINED CMAKE_TOOLCHAIN_FILE)
    get_filename_component(TOOLCHAIN_FILENAME "${CMAKE_TOOLCHAIN_FILE}" NAME)
    string(REGEX MATCH "^toolchain-([a-zA-Z0-9_]+)\\.cmake$" _ "${TOOLCHAIN_FILENAME}")
    if(CMAKE_MATCH_1)
        set(TARGET ${CMAKE_MATCH_1} CACHE STRING "Target platform inferred from toolchain file")
    endif()
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(NOTICE "Setting build type to 'Debug' as none was specified.")
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build, e.g. Debug, Release, RelWithDebInfo, MinSizeRel." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

message("DALI-2-MQTT v${PROJVER} (${PROJBRANCH})")
set(targets "esp32s3" "esp32c6")

if("${TARGET}" IN_LIST targets)
    message(NOTICE "Target platform: " ${TARGET})
    message(NOTICE "Build type: " ${CMAKE_BUILD_TYPE})
    message(STATUS "Performing configuration stage")
    include(scripts/configure_build.cmake)
else ()
    message(FATAL_ERROR "Call cmake with -DCMAKE_TOOLCHAIN_FILE <toolchain>.")
endif ()
include(scripts/auxiliary_targets.cmake)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY CMAKE_DEFAULT_TARGET daliMQTT)
