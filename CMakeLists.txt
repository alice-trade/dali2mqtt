cmake_minimum_required(VERSION 3.30)
project(daliMQTT VERSION 0.2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED TRUE)

message("daliMQTT version " ${CMAKE_PROJECT_VERSION})
set(targets "esp32")

if("${TARGET}" IN_LIST targets)
    message("Target platform: " ${TARGET})
    message("Build type: " ${CMAKE_BUILD_TYPE})
    message(STATUS "Performing configuration stage")
    add_subdirectory(${TARGET})
else ()
    message(FATAL_ERROR "Unsupported platform. Call cmake with -DTARGET <target>.")
endif ()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY CMAKE_DEFAULT_TARGET daliMQTT)
