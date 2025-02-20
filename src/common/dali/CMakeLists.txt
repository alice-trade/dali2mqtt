project(DALI LANGUAGES CXX)
add_library(DALI STATIC
        "main.cpp")

target_include_directories(DALI PUBLIC include)
target_link_libraries(DALI INTERFACE daliC)