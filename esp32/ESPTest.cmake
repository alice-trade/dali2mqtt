set (TEST_APP "Test ${CMAKE_PROJECT_NAME}")
add_executable(TEST_APP)
target_sources(TEST_APP PRIVATE
            ${CMAKE_SOURCE_DIR}/tests/esp32/modules/config.c
)


