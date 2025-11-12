# ESP 32 Build Process

include($ENV{IDF_PATH}/tools/cmake/idf.cmake)
include(../scripts/fetch_3rdparties.cmake)


message("Build for: " ${CMAKE_BUILD_TYPE})
message("Uses Sdkconfig: " ${CMAKE_CURRENT_SOURCE_DIR} "/" ${CMAKE_BUILD_TYPE} "/" sdkconfig)
set(PLATFORM_MODULES
        freertos
        esptool_py
        json
        spiffs
        mqtt
        log
        esp_event
        esp_wifi
        mdns
        esp_http_server
        driver
        esp_netif
        nvs_flash
        esp_timer
        Kconfig
)

if(BUILD_UNITY)
    message("----------UNITY BUILD-------")
    list(APPEND PLATFORM_MODULES unity esp_http_client)
endif()


idf_build_component(${ESP_BUILD_UTILS_PATH}/Kconfig)
idf_build_component(${ESP_PROTO_BASEDIR}/mdns)


idf_build_process(esp32s3
        COMPONENTS
            ${PLATFORM_MODULES}
        SDKCONFIG
            ${ESP_BUILD_UTILS_PATH}/${CMAKE_BUILD_TYPE}/sdkconfig
        PROJECT_VER ${CMAKE_PROJECT_VERSION}
        PROJECT_DIR ${CMAKE_SOURCE_DIR}
        SDKCONFIG_DEFAULTS ${ESP_BUILD_UTILS_PATH}/${CMAKE_BUILD_TYPE}/sdkconfig.default
        BUILD_DIR ${CMAKE_BINARY_DIR}
)