project(libDaliMQTT LANGUAGES C)

add_library(libDaliMQTT STATIC)

set(TEST_BASEDIR ${CMAKE_SOURCE_DIR}/src/esp32/tests)
set(MODULES_BASEDIR ${CMAKE_SOURCE_DIR}/src/esp32/modules)

set(MODULES_SOURCES
        ${MODULES_BASEDIR}/config/config.c
        ${MODULES_BASEDIR}/mqtt/mqtt.c
        ${MODULES_BASEDIR}/wifi/wifi_manager.c
        ${MODULES_BASEDIR}/dali/dali_interface.c
)

set(TEST_SOURCES
        ${TEST_BASEDIR}/config/config.test.c
        #${TEST_BASEDIR}/dali/dali_interface.test.c
)

target_sources(libDaliMQTT PRIVATE ${MODULES_SOURCES})
target_include_directories(libDaliMQTT PUBLIC ./config ./mqtt ./dali ./wifi)
target_link_libraries(libDaliMQTT PUBLIC
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