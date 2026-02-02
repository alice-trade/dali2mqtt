project(DaliMQTT-Core LANGUAGES CXX)

add_library(DaliMQTT-Core STATIC)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pch.hxx")
    target_precompile_headers(DaliMQTT-Core PRIVATE pch.hxx)
endif()

string(TIMESTAMP PROJ_BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ")

# Target definitions
target_compile_definitions(DaliMQTT-Core PUBLIC  $<$<CONFIG:Debug>:_DEBUG_DALIMQTT_> DALIMQTT_VERSION=\"${PROJVER}\" DALIMQTT_CONFIGURED_TIMESTAMP=\"${PROJ_BUILD_TIMESTAMP}\")

set(MODULE_DIRS
        system
        wifi
        webui
        webui/api
        dali
        dali/driver
        mqtt
)

set(PROJECT_SOURCES "")
foreach(DIR IN LISTS MODULE_DIRS)
    file(GLOB_RECURSE DIR_SRCS
            "${CMAKE_CURRENT_SOURCE_DIR}/${DIR}/*.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/${DIR}/*.cxx"
    )
    list(APPEND PROJECT_SOURCES ${DIR_SRCS})
endforeach()

message(STATUS "Found DaliMQTT source files: ${PROJECT_SOURCES}")

target_sources(DaliMQTT-Core PRIVATE ${PROJECT_SOURCES})

target_include_directories(DaliMQTT-Core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(DaliMQTT-Core PUBLIC
        idf::nvs_flash
        idf::mqtt
        idf::log
        idf::esp_event
        idf::json
        idf::driver
        idf::esp_netif
        idf::freertos
        idf::esp_timer
        idf::spiffs
        idf::mdns
        idf::esp_wifi
        idf::cxx
        idf::esp_http_server
        idf::esp_https_ota
        idf::app_update
)