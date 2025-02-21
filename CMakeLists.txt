cmake_minimum_required(VERSION 3.30)
project(daliMQTT LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)

set(CMAKE_C_STANDARD_REQUIRED TRUE)

message("daliMQTT ver." ${PROJECT_VERSION})
set(targets "esp32")

if("${TARGET}" IN_LIST targets)
    message("Target platform: " ${TARGET})
    message("Toolchain: " ${CMAKE_TOOLCHAIN_FILE})
    message("Build type: " ${CMAKE_BUILD_TYPE})
    message(STATUS performing build stage)
    add_subdirectory(${TARGET})
else ()
    message(FATAL_ERROR "Unsupported platform")
endif ()