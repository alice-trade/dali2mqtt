project(DALI LANGUAGES CXX)
add_library(DALI STATIC
        "${CMAKE_CURRENT_LIST_DIR}/src/DALI.cpp")

target_include_directories(DALI PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src)
target_link_libraries(DALI PRIVATE daliC)


