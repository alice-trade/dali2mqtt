project(DaliMQTT-Core LANGUAGES CXX)

add_library(DaliMQTT-Core STATIC)

set(MODULE_DIRS
        config
        wifi
        lifecycle
        webui/cxx
        dali
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

target_include_directories(DaliMQTT-Core PUBLIC ${MODULE_DIRS} include-add)
target_link_libraries(DaliMQTT-Core PUBLIC
        daliC
        idf::nvs_flash
        idf::mqtt
        idf::log
        idf::esp_event
        idf::json
        idf::driver
        idf::esp_netif
        idf::freertos
        idf::spiffs
        idf::mdns
        idf::esp_wifi
        idf::cxx
        idf::esp_http_server
)