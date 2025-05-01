project(daliC LANGUAGES C)
add_library(daliC STATIC
        "dali.c")

target_include_directories(daliC PUBLIC include)

target_link_libraries(daliC PRIVATE
        idf::freertos
        idf::driver
        idf::esp_event
        idf::log
)