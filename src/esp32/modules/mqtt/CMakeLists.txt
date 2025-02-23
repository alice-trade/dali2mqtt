add_library(mqtt STATIC
        mqtt_client/mqtt_client.cpp
        mqtt_protocol/mqtt_protocol.cpp
)

target_include_directories(mqtt PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/mqtt_client
        ${CMAKE_CURRENT_LIST_DIR}/mqtt_protocol
)

target_link_libraries(mqtt
        esp_mqtt
        esp_event
        esp_log
)