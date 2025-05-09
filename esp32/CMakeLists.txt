message(${CMAKE_C_COMPILER})
message(${PROJECT_SOURCE_DIR})

include(etc/idf_buildup.cmake)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(app ${CMAKE_PROJECT_NAME})

add_subdirectory(../src/esp32/driver/dalic dalic)
add_subdirectory(../src/esp32/modules daliMQTTModules)

include(etc/build_firmware.cmake)
include(etc/make_esp_test_target.cmake)

