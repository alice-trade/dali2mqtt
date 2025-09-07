# ESP 32 Build Process

include($ENV{IDF_PATH}/tools/cmake/idf.cmake)
string(TOUPPER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPEU)

message("Build for" ${CMAKE_BUILD_TYPEU})

set(PLATFORM_MODULES
        freertos
        esptool_py
        json
        spiffs
        mqtt
        log
        esp_event
        esp_wifi
        esp_http_server
        driver
        esp_netif
        nvs_flash
        Kconfig
)

if(BUILD_UNITY)
    message("----------UNITY BUILD-------")
    list(APPEND PLATFORM_MODULES unity)
endif()

idf_build_component(${ESP_BUILD_UTILS_PATH}/Kconfig)

idf_build_process(esp32
        COMPONENTS
            ${PLATFORM_MODULES}
        SDKCONFIG
            ${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig
        PROJECT_VER ${CMAKE_PROJECT_VERSION}
        PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR}
        #SDKCONFIG_DEFAULTS ${ESP_BUILD_UTILS_PATH}/sdkconfig.default
        BUILD_DIR ${CMAKE_BINARY_DIR}
)