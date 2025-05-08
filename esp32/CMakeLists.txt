include($ENV{IDF_PATH}/tools/cmake/idf.cmake)

message(${CMAKE_C_COMPILER})
message(${PROJECT_SOURCE_DIR})


idf_build_component(./Kconfig)

idf_build_process(esp32
        COMPONENTS freertos esptool_py json mqtt log esp_event esp_wifi driver unity esp_netif nvs_flash Kconfig
        SDKCONFIG ${CMAKE_CURRENT_LIST_DIR}/sdkconfig
        SDKCONFIG_DEFAULTS ${CMAKE_CURRENT_LIST_DIR}/sdkconfig.default
        BUILD_DIR ${CMAKE_BINARY_DIR}
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(app ${CMAKE_PROJECT_NAME})



add_subdirectory(../src/esp32/driver/dalic dalic)
add_subdirectory(../src/esp32/modules daliMQTTModules)

add_executable(${app} ../src/esp32/main.c)



target_link_libraries(${app} idf::freertos idf::newlib idf::log idf::nvs_flash libDaliMQTT idf::esp_event)
idf_build_executable(${app})

#include(ESPTest.cmake)

