add_subdirectory(modules modules/esp32)
add_subdirectory(driver modules/esp32/driver)
add_subdirectory(../common modules/common)

if (${TARGET} MATCHES esp32)
    target_link_libraries(DALI PRIVATE idf::newlib idf::cxx idf::log DALIEsp32Modules daliC)
endif ()

add_subdirectory(runtime runtime)
