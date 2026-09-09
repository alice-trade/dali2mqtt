cmake_minimum_required(VERSION 3.20)
project(DaliMQTTHost C CXX)

if(NOT PROJDIR)
    set(PROJDIR ${CMAKE_SOURCE_DIR}/../..)
endif ()

include(${PROJDIR}/scripts/common.cmake)
set(Dependencies_Platform_HostTests ON)
include(${PROJDIR}/scripts/dependencies.cmake)

add_library(firmware_host_logic OBJECT
        mocks/esp_stubs.cxx
        ${PROJDIR}/src/DaliMQTT/system/ConfigStore.cxx
        ${PROJDIR}/src/DaliMQTT/dali/DaliBusEngine.cxx
        ${PROJDIR}/src/DaliMQTT/dali/DaliDeviceRegistry.cxx
        ${PROJDIR}/src/DaliMQTT/dali/RmtDaliTransceiver.cxx
        ${PROJDIR}/src/DaliMQTT/mqtt/MqttClient.cxx
        ${PROJDIR}/src/DaliMQTT/mqtt/HomeAssistantDiscovery.cxx
)

target_include_directories(firmware_host_logic PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/mocks
        ${PROJDIR}/src/DaliMQTT
)

target_compile_definitions(firmware_host_logic PUBLIC
        CONFIG_DALI2MQTT_NVS_NAMESPACE="dali_cfg"
        CONFIG_DALI2MQTT_MQTT_BASE_TOPIC="dali_bridge"
        CONFIG_DALI2MQTT_MQTT_AVAILABILITY_TOPIC="/status"
        CONFIG_DALI2MQTT_MQTT_PAYLOAD_ONLINE="online"
        CONFIG_DALI2MQTT_MQTT_PAYLOAD_OFFLINE="offline"
        CONFIG_DALI2MQTT_WEBUI_DEFAULT_MDNS_DOMAIN="dalimqtt"
        CONFIG_DALI2MQTT_WEBUI_DEFAULT_USER="admin"
        CONFIG_DALI2MQTT_WEBUI_DEFAULT_PASS="admin"
        CONFIG_DALI2MQTT_WEBUI_SPIFFS_PARTITION_LABEL="web_storage"
        CONFIG_DALI2MQTT_DALI_RX_PIN=16
        CONFIG_DALI2MQTT_DALI_TX_PIN=17
        CONFIG_DALI2MQTT_DALI_DEFAULT_POLL_INTERVAL_MS=300000
        CONFIG_DALI2MQTT_DEFAULT_OTA_URL="https://github.com/alice-trade/dali2mqtt/releases/latest"
        DALIMQTT_VERSION="test"
        _HOST_TESTING_
)

target_link_libraries(firmware_host_logic PUBLIC
        etl::etl
        ArduinoJson
)

add_executable(dali_host_tests
        test_dali_long_addr.cxx
        test_dali_protocol.cxx
        test_dali_domain.cxx
        test_config_store.cxx
        test_dali_sniffer.cxx
        test_ha_discovery.cxx
)

target_link_libraries(dali_host_tests PRIVATE
        firmware_host_logic
        Catch2::Catch2WithMain
)