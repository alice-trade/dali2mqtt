project(libDALI LANGUAGES CXX)

# IN DEVELOPMENT REIMPLIMENTATION OF DALI_LIB
#TODO: TO BE REWRITTEN TO RMT
add_library(libDALI STATIC
        "libdali/DALI_Lib.cpp"
)
target_include_directories(libDALI
        PRIVATE
        libdali
        INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(libDALI PRIVATE
        idf::freertos
        idf::driver
        idf::esp_event
        idf::log
        idf::esp_timer
)