set (TEST_APP "Test-${CMAKE_PROJECT_NAME}")
add_executable(${TEST_APP})


target_sources(${TEST_APP} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/esp32/test_cases.c
            ${TEST_SOURCES}
)
target_link_libraries(${TEST_APP} PRIVATE libDaliMQTT idf::unity)

idf_build_executable(${TEST_APP})