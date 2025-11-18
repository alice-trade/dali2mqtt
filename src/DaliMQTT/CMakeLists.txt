project(DaliMQTT-Core LANGUAGES CXX)

add_library(DaliMQTT-Core STATIC)

target_precompile_headers(DaliMQTT-Core PRIVATE pch.hxx)

string(TIMESTAMP PROJ_BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ")
target_compile_definitions(DaliMQTT-Core PUBLIC  $<$<CONFIG:Debug>:_DEBUG_DALIMQTT_> DALIMQTT_VERSION=\"${PROJVER}\" DALIMQTT_CONFIGURED_TIMESTAMP=\"${PROJ_BUILD_TIMESTAMP}\")

set(MODULE_DIRS
        config
        wifi
        lifecycle
        webui/cxx
        webui/cxx/api
        dali
        dali/driver
        mqtt
)

set(MODULE_SOURCES "")
foreach(DIR IN LISTS MODULE_DIRS)
    file(GLOB C_FILES "${CMAKE_CURRENT_SOURCE_DIR}/${DIR}/*.c")
    file(GLOB CXX_FILES "${CMAKE_CURRENT_SOURCE_DIR}/${DIR}/*.cxx")
    list(APPEND MODULE_SOURCES ${C_FILES} ${CXX_FILES})
endforeach()

message(STATUS "Found DaliMQTT source files: ${MODULE_SOURCES}")

target_sources(DaliMQTT-Core PRIVATE ${MODULE_SOURCES})

target_include_directories(DaliMQTT-Core PRIVATE ${MODULE_DIRS} include INTERFACE ..)
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
)