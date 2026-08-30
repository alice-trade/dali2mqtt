cmake_minimum_required(VERSION 3.22)
project(DaliMQTTTestFirmware LANGUAGES C CXX ASM)

include(${PROJDIR}/scripts/common.cmake)
include(${PROJDIR}/scripts/idf_buildup.cmake)

add_subdirectory(${PROJDIR}/src/DaliMQTT MainSourceTree)

set(TEST_APP_NAME "${CMAKE_PROJECT_NAME}.elf")
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

