add_executable(${app} ${CMAKE_SOURCE_DIR}/src/esp32/main.c)

target_link_libraries(${app} PRIVATE idf::freertos libDaliMQTT)
idf_build_executable(${app})