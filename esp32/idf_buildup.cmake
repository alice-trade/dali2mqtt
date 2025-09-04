# ESP 32 Build Process

include($ENV{IDF_PATH}/tools/cmake/idf.cmake)

idf_build_component(${ESP_BUILD_UTILS_PATH}/Kconfig)

idf_build_process(esp32
        COMPONENTS freertos esptool_py json mqtt log esp_event esp_wifi esp_http_server driver unity esp_netif nvs_flash Kconfig
        SDKCONFIG ${ESP_BUILD_UTILS_PATH}/sdkconfig
        PROJECT_VER ${CMAKE_PROJECT_VERSION}
        #SDKCONFIG_DEFAULTS ${ESP_BUILD_UTILS_PATH}/sdkconfig.default
        BUILD_DIR ${CMAKE_BINARY_DIR}
)