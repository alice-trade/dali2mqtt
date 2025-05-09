project(libDaliMQTT LANGUAGES C)

add_library(libDaliMQTT STATIC)

set(MODULES_BASEDIR ${CMAKE_SOURCE_DIR}/src/esp32/modules)

set(MODULES_SOURCES
        ${MODULES_BASEDIR}/config/config.c
        ${MODULES_BASEDIR}/mqtt/mqtt.c
        ${MODULES_BASEDIR}/wifi/wifi_manager.c
        ${MODULES_BASEDIR}/dali/dali_interface.c
)



target_sources(libDaliMQTT PRIVATE ${MODULES_SOURCES})
target_include_directories(libDaliMQTT PUBLIC ./config ./mqtt ./dali ./wifi)

target_link_libraries(libDaliMQTT PUBLIC
        daliC
        idf::nvs_flash
        idf::mqtt
        idf::log
        idf::esp_event
        idf::json
        idf::driver
        idf::esp_netif
        idf::freertos
        idf::esp_wifi
)