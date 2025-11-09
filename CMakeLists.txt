cmake_minimum_required(VERSION 3.20)
project(
        daliMQTT
        VERSION 0.10.0
        DESCRIPTION "DALI-to-MQTT bridge for ESP platform"
        LANGUAGES C CXX)

include(scripts/common.cmake)
set(PROJVER ${CMAKE_PROJECT_VERSION})

message("daliMQTT version " ${CMAKE_PROJECT_VERSION})
set(targets "esp32s3")

if("${TARGET}" IN_LIST targets)
    message("Target platform: " ${TARGET})
    message("Build type: " ${CMAKE_BUILD_TYPE})
    message(STATUS "Performing configuration stage")
    add_subdirectory(${TARGET})
else ()
    message(FATAL_ERROR "Unsupported platform. Call cmake with -DTARGET <target> and -DCMAKE_TOOLCHAIN_FILE <toolchain>.")
endif ()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY CMAKE_DEFAULT_TARGET daliMQTT)
