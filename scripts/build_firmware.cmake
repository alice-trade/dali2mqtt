add_executable(${app} ${CMAKE_SOURCE_DIR}/src/main.cxx)

target_link_libraries(${app} PRIVATE DaliMQTT-Core)


idf_build_executable(${app})