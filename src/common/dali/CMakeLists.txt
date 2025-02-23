project(DALI LANGUAGES CXX)
add_library(DALI STATIC
        "src/DALI.cpp")

target_include_directories(DALI PUBLIC src)
target_link_libraries(DALI INTERFACE daliC)