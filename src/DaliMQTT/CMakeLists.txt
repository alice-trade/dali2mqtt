# Copyright (c) 2025-2026 Alice-Trade Inc.
# SPDX-License-Identifier: GPL-3.0-or-later
add_library(DaliMQTT-Core STATIC)

target_compile_options(DaliMQTT-Core PRIVATE
        -Wall
        -Wextra
        -Wshadow
        -Wnon-virtual-dtor
        -Wunused
        -Woverloaded-virtual
        -Wformat=2
        -Wno-missing-field-initializers
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pch.hxx")
    target_precompile_headers(DaliMQTT-Core PUBLIC pch.hxx)
endif()
string(TIMESTAMP PROJ_BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ")

# Target definitions
target_compile_definitions(DaliMQTT-Core PUBLIC
        $<$<CONFIG:Debug>:_DEBUG_DALIMQTT_>
        DALIMQTT_VERSION=\"${PROJVER}\"
)

set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/webui/api/InfoController.cxx"
        PROPERTIES
        COMPILE_DEFINITIONS "DALIMQTT_CONFIGURED_TIMESTAMP=\"${PROJ_BUILD_TIMESTAMP}\""
)
target_include_directories(DaliMQTT-Core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

add_subdirectory(system)
add_subdirectory(dali)
add_subdirectory(mqtt)
add_subdirectory(webui)
add_subdirectory(network)


target_link_libraries(DaliMQTT-Core PUBLIC
        idf::nvs_flash
        idf::log
        idf::esp_event
        ArduinoJson
        etl::etl
        idf::esp_ringbuf
        idf::esp_driver_gpio
        idf::esp_driver_rmt
        idf::esp_netif
        idf::freertos
        idf::esp_timer
        idf::esp-littlefs-src
        idf::esp-mqtt-src
        idf::mdns
        idf::cxx
        idf::esp_https_ota
        idf::app_update
)