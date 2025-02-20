project(daliC LANGUAGES CXX)
add_library(daliC STATIC
        "dali.c")

target_include_directories(daliC PUBLIC include)