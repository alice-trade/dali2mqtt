project(DALIEsp32Modules LANGUAGES C CXX)


set(SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/config/ConfigManager.cpp
        ${CMAKE_CURRENT_LIST_DIR}//mqtt/mqtt_client/mqtt_client.cpp
        ../../common/dali/src/mqtt_protocol.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ethernet/ethernet.c
)

add_library(DALIEsp32Modules STATIC ${SOURCES})

target_include_directories(DALIEsp32Modules PUBLIC ./config ./mqtt/mqtt_protocol ./mqtt/mqtt_client ./ethernet)

target_link_libraries(DALIEsp32Modules PRIVATE
        idf::mqtt
        idf::esp_event
        idf::log
        idf::driver
        idf::esp_netif
        idf::esp_eth
        idf::cxx
        idf::nvs_flash
)