# idf common build

include($ENV{IDF_PATH}/tools/cmake/idf.cmake)

message(NOTICE "IDF framework at: " $ENV{IDF_PATH})
message(NOTICE "IDF Version: ${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}")

if((IDF_VERSION_MAJOR LESS 5))
    message(FATAL_ERROR "Build requires ESP-IDF v5.x minimum. Current version is not supported")
endif()

include(${PROJDIR}/scripts/dependencies.cmake)


message("Build for: " ${CMAKE_BUILD_TYPE})
message("Uses Sdkconfig: " ${CMAKE_CURRENT_SOURCE_DIR} "/" ${CMAKE_BUILD_TYPE} "/" sdkconfig)
set(PLATFORM_MODULES
        freertos
        esptool_py
        esp-mqtt-src
        log
        esp_event
        esp_wifi
        mdns
        esp_http_server
        esp_http_client
        esp_driver_gpio
        esp_driver_rmt
        esp_ringbuf
        hal
        esp_netif
        esp_https_ota
        app_update
        nvs_flash
        esp_timer
        Kconfig
        esp-littlefs-src
)

if(BUILD_UNITY)
    message("-------- TEST BACKEND BUILD -------")
    list(APPEND PLATFORM_MODULES unity)
endif()

idf_build_component(${ESP_BUILD_UTILS_PATH}/../../Kconfig)
idf_build_component(${ESP_PROTO_BASEDIR}/mdns)
idf_build_component(${esp-littlefs_SOURCE_DIR})
idf_build_component(${esp-mqtt_SOURCE_DIR})

idf_build_process(${TARGET}
        COMPONENTS
        ${PLATFORM_MODULES}
        SDKCONFIG
        ${ESP_BUILD_UTILS_PATH}/${CMAKE_BUILD_TYPE}/sdkconfig
        PROJECT_VER ${CMAKE_PROJECT_VERSION}
        PROJECT_DIR ${CMAKE_SOURCE_DIR}
        SDKCONFIG_DEFAULTS ${ESP_BUILD_UTILS_PATH}/${CMAKE_BUILD_TYPE}/sdkconfig.default
        BUILD_DIR ${CMAKE_BINARY_DIR}
)