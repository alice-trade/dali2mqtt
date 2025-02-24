project(DALI LANGUAGES CXX)
add_library(DALI STATIC
        "${CMAKE_CURRENT_LIST_DIR}/src/dali_commander.cpp"
        "src/mqtt_protocol.cpp"
        "src/DALI.cpp")

target_include_directories(DALI PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src)

