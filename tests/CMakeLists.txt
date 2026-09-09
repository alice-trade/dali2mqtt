cmake_minimum_required(VERSION 3.22)
project(DaliMQTTTestFirmware LANGUAGES C CXX)

include(${PROJDIR}/scripts/common.cmake)
include(${PROJDIR}/scripts/idf_buildup.cmake)

add_subdirectory(${PROJDIR}/src/DaliMQTT MainSourceTree)
set(TEST_APP_NAME "${CMAKE_PROJECT_NAME}.elf")

add_executable(${TEST_APP_NAME}
        test_cases.cxx
)

target_sources(${TEST_APP_NAME} PRIVATE
        system/ConfigStore.test.cxx
        dali/DaliHardwareAndRegistry.test.cxx
        mqtt/MqttBridge.test.cxx
        wifi/WifiSystem.test.cxx
        webui/HttpStream.test.cxx
)
target_compile_definitions(${TEST_APP_NAME} PRIVATE
        CONFIG_DALI2MQTT_NVS_NAMESPACE="tst_cfg"
        DALIMQTT_REG_NVS_NAMESPACE="tst_reg"
)
target_link_libraries(${TEST_APP_NAME} PRIVATE
        DaliMQTT-Core
        idf::unity
        idf::esp_http_client
)

idf_build_executable(${TEST_APP_NAME})

