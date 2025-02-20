cmake_minimum_required(VERSION 3.30)
project(daliMQTT LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)

set(CMAKE_C_STANDARD_REQUIRED TRUE)


# Build for ESP32
IF((${CMAKE_BUILD_TYPE} MATCHES Debug-ESP32) OR (${CMAKE_BUILD_TYPE} MATCHES ESP32-Debug) OR (${CMAKE_BUILD_TYPE} MATCHES DEBUG-ESP32))
    message("debug mode for esp32")
    add_subdirectory(esp-idf)
elseif((${CMAKE_BUILD_TYPE} MATCHES Release-ESP32) OR (${CMAKE_BUILD_TYPE} MATCHES ESP32-Release) OR (${CMAKE_BUILD_TYPE} MATCHES RELEASE-ESP32))
message("release mode for esp32")
    add_subdirectory(esp-idf)
else()
    message("Unsupported Build Target")
endif ()