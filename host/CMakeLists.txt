cmake_minimum_required(VERSION 3.20)
project(HostUnitTests CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${PROJDIR}/scripts/common.cmake)
set(Dependencies_Platform_HostTests ON)
include(${PROJDIR}/scripts/dependencies.cmake)


add_library(firmware_host_logic OBJECT)

target_include_directories(firmware_host_logic PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/mocks
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/DaliMQTT/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/DaliMQTT
)

target_compile_definitions(firmware_host_logic PUBLIC
        CONFIG_DALI2MQTT_NVS_NAMESPACE="dali_cfg"
        CONFIG_DALI2MQTT_MQTT_BASE_TOPIC="dali_bridge"
        DALIMQTT_VERSION="0.15.0-test"
        _HOST_TESTING_
)

target_link_libraries(firmware_host_logic PUBLIC
        etl::etl
        ArduinoJson
)

add_executable(dali_host_tests
        test_dali_protocol.cxx
        test_dali_address_map.cxx
)

target_link_libraries(dali_host_tests PRIVATE
        firmware_host_logic
        Catch2::Catch2WithMain
)
