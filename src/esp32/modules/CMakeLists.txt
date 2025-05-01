project(libDaliMQTT LANGUAGES C)

set(SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/config/config.c
        ${CMAKE_CURRENT_LIST_DIR}/mqtt/mqtt.c
        ${CMAKE_CURRENT_LIST_DIR}/wifi/wifi_manager.c
        ${CMAKE_CURRENT_LIST_DIR}/dali/dali_interface.c
)

add_library(libDaliMQTT STATIC ${SOURCES})

target_include_directories(libDaliMQTT PUBLIC ./config ./mqtt ./dali ./wifi)
target_link_libraries(libDaliMQTT PRIVATE
        daliC
        idf::mqtt
        idf::esp_event
        idf::log
        idf::json
        idf::driver
        idf::esp_netif
        idf::nvs_flash
        idf::esp_wifi
)