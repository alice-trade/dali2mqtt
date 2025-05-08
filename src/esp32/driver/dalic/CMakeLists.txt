project(daliC LANGUAGES C)
add_library(daliC STATIC
        "dali.c")

target_include_directories(daliC
        PRIVATE
        include
        INTERFACE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/esp32/driver>
)

target_link_libraries(daliC PRIVATE
        idf::freertos
        idf::driver
        idf::esp_event
        idf::log
)
