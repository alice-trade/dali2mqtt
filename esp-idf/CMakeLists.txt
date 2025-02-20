include($ENV{IDF_PATH}/tools/cmake/idf.cmake)

idf_build_process("${TARGET}"
        # try and trim the build; additional components
        # will be included as needed based on dependency tree
        #
        # although esptool_py does not generate static library,
        # processing the component is needed for flashing related
        # targets and file generation
        COMPONENTS freertos esptool_py unity
        esp-idf/sdkconfig esp-idf/sdkconfig
        BUILD_DIR ${CMAKE_BINARY_DIR})

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(elf_file ${CMAKE_PROJECT_NAME}.elf)

add_subdirectory(/src/esp32)


