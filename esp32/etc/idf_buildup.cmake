include($ENV{IDF_PATH}/tools/cmake/idf.cmake)

idf_build_component(${CMAKE_CURRENT_SOURCE_DIR}/Kconfig)

idf_build_process(esp32
        COMPONENTS freertos esptool_py json mqtt log esp_event esp_wifi driver unity esp_netif nvs_flash Kconfig
        SDKCONFIG ${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig
        SDKCONFIG_DEFAULTS ${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.default
        BUILD_DIR ${CMAKE_BINARY_DIR}
)