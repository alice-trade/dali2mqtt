cmake_minimum_required(VERSION 3.20)
project(
        daliMQTT
        VERSION 0.11.0
        DESCRIPTION "DALI-to-MQTT bridge for ESP platform"
        LANGUAGES C CXX)

include(scripts/common.cmake)
set(PROJVER ${CMAKE_PROJECT_VERSION})

if(NOT DEFINED TARGET AND DEFINED CMAKE_TOOLCHAIN_FILE)
    get_filename_component(TOOLCHAIN_FILENAME "${CMAKE_TOOLCHAIN_FILE}" NAME)
    string(REGEX MATCH "^toolchain-([a-zA-Z0-9_]+)\\.cmake$" _ "${TOOLCHAIN_FILENAME}")
    if(CMAKE_MATCH_1)
        set(TARGET ${CMAKE_MATCH_1} CACHE STRING "Target platform inferred from toolchain file")
    endif()
endif()

message("daliMQTT version " ${CMAKE_PROJECT_VERSION})
set(targets "esp32s3" "esp32c6")

if("${TARGET}" IN_LIST targets)
    message("Target platform: " ${TARGET})
    message("Build type: " ${CMAKE_BUILD_TYPE})
    message(STATUS "Performing configuration stage")
    add_subdirectory(${TARGET})
else ()
    message(FATAL_ERROR "Call cmake with -DCMAKE_TOOLCHAIN_FILE <toolchain>.")
endif ()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY CMAKE_DEFAULT_TARGET daliMQTT)
