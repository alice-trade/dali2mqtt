cmake_minimum_required(VERSION 3.16)
project(DaliMQTTTests LANGUAGES CXX)

include(${PROJDIR}/scripts/common.cmake)
include(${PROJDIR}/scripts/idf_buildup.cmake)

add_subdirectory(${PROJDIR}/src/DaliMQTT daliMQTTModules)

set(TEST_APP_NAME "daliMQTTTestESP")
file(GLOB_RECURSE TEST_CASE_FILES "**/*.test.cxx")

message(STATUS "Found test files: ${TEST_CASE_FILES}")

add_executable(${TEST_APP_NAME}
        test_cases.cxx
        ${TEST_CASE_FILES}
)

target_link_libraries(${TEST_APP_NAME} PRIVATE
        DaliMQTT-Core
        idf::unity
        idf::esp_http_client
)

idf_build_executable(${TEST_APP_NAME})

