project(DALIEsp32Modules LANGUAGES C)


set(SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/config/ConfigManager.c
        ${CMAKE_CURRENT_LIST_DIR}//mqtt/mqtt_client/mqtt_client.c
        ${CMAKE_CURRENT_LIST_DIR}/ethernet/ethernet.c
)

add_library(DALIEsp32Modules STATIC ${SOURCES})

target_include_directories(DALIEsp32Modules PUBLIC ./config ./mqtt/mqtt_client ./ethernet)

target_link_libraries(DALIEsp32Modules PRIVATE
        idf::mqtt
        idf::esp_event
        idf::log
        idf::driver
        idf::esp_netif
        idf::esp_eth
        idf::nvs_flash
)