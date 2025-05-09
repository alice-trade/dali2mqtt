add_executable(${app} ${CMAKE_SOURCE_DIR}/src/esp32/main.c)

target_link_libraries(${app} PRIVATE libDaliMQTT)


idf_build_executable(${app})