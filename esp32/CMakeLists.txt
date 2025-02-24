include($ENV{IDF_PATH}/tools/cmake/idf.cmake)

message(${CMAKE_C_COMPILER})
message(${PROJECT_SOURCE_DIR})

idf_build_process(esp32
        # try and trim the build; additional components
        # will be included as needed based on dependency tree
        #
        # although esptool_py does not generate static library,
        # processing the component is needed for flashing related
        # targets and file generation
        COMPONENTS freertos esptool_py mqtt log esp_event esp_eth driver unity esp_netif nvs_flash cxx
        SDKCONFIG esp32/sdkconfig
        BUILD_DIR ${CMAKE_BINARY_DIR})

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(elf_file ${CMAKE_PROJECT_NAME}.elf)

add_subdirectory(../src/esp32 esp32)



add_executable(${elf_file} ../src/esp32/main.cpp)

if (${TARGET} MATCHES esp32)
    target_link_libraries(DALI PRIVATE idf::newlib idf::cxx idf::log)
endif ()

target_compile_options(${elf_file} PRIVATE -std=gnu++20)

target_link_libraries(${elf_file} idf::freertos idf::newlib idf::log idf::cxx)
# Attach additional targets to the executable file for flashing,
# linker script generation, partition_table generation, etc.
idf_build_executable(${elf_file})

