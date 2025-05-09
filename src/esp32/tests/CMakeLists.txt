set(TEST_BASEDIR ${CMAKE_SOURCE_DIR}/src/esp32/tests)
set(TEST_SOURCES
        ${TEST_BASEDIR}/config/config.test.c
        #${TEST_BASEDIR}/dali/dali_interface.test.c
)

include(${CMAKE_SOURCE_DIR}/esp32/etc/make_esp_test_target.cmake)