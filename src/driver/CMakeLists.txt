project(daliC LANGUAGES C)
add_library(daliC STATIC
        "dalic/dali.c")

target_include_directories(daliC
        PRIVATE
        dalic/include
        INTERFACE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/driver>
)

target_link_libraries(daliC PRIVATE
        idf::freertos
        idf::driver
        idf::esp_event
        idf::log
)
