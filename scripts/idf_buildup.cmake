# idf common build

include($ENV{IDF_PATH}/tools/cmake/idf.cmake)
include(${PROJDIR}/scripts/overrides.cmake)

message(NOTICE "IDF framework at: " $ENV{IDF_PATH})
message(NOTICE "IDF Version: ${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}")

if((IDF_VERSION_MAJOR LESS 5))
    message(FATAL_ERROR "Build requires ESP-IDF v5.x minimum. Current version is not supported")
endif()

message(STATUS "External dependencies are being processed...")
set(Dependencies_Platform_ESP ON)
include(${PROJDIR}/scripts/dependencies.cmake)

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
set(SDKCONFIG_DEFAULTS_LIST "${ESP_BUILD_UTILS_PATH}/${CMAKE_BUILD_TYPE}/sdkconfig.default")
set(CURRENT_SDKCONFIG "${ESP_BUILD_UTILS_PATH}/${CMAKE_BUILD_TYPE}/sdkconfig")

if(BUILD_UNITY)
    set(CURRENT_SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
    if(EXISTS "${PROJDIR}/tests/sdkconfig.test")
        list(APPEND SDKCONFIG_DEFAULTS_LIST "${PROJDIR}/tests/sdkconfig.test")
    endif()
    message("-------- TEST BACKEND BUILD -------")
    list(APPEND PLATFORM_MODULES unity)
endif()

message("Build for: " ${CMAKE_BUILD_TYPE})
message(STATUS "Uses Sdkconfig: ${CURRENT_SDKCONFIG}")

idf_build_component(${ESP_BUILD_UTILS_PATH}/../../Kconfig)
idf_build_component(${ESP_PROTO_BASEDIR}/mdns)
idf_build_component(${esp-littlefs_SOURCE_DIR})
idf_build_component(${esp-mqtt_SOURCE_DIR})

idf_build_process(${TARGET}
        COMPONENTS
        ${PLATFORM_MODULES}
        SDKCONFIG
        ${CURRENT_SDKCONFIG}
        PROJECT_VER ${CMAKE_PROJECT_VERSION}
        PROJECT_DIR ${PROJDIR}
        SDKCONFIG_DEFAULTS "${SDKCONFIG_DEFAULTS_LIST}"
        BUILD_DIR ${CMAKE_BINARY_DIR}
)