add_library(eth STATIC
        "ethernet.c" "ethernet.h")

target_include_directories(eth PUBLIC include)