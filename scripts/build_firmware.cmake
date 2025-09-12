add_executable(${app} ${CMAKE_SOURCE_DIR}/src/DaliMQTT/main.cxx)
target_compile_definitions(${app} PRIVATE DALIMQTT_VERSION=\"${PROJECT_VERSION}\")

target_link_libraries(${app} PRIVATE DaliMQTT-Core)


idf_build_executable(${app})