add_subdirectory(modules)
add_subdirectory(../../common)


add_executable(${elf_file} /src/esp32/main.cpp)
target_compile_options(${elf_file} PRIVATE -std=gnu++20)

target_link_libraries(${elf_file} idf::freertos idf::spi_flash idf::newlib)
# Attach additional targets to the executable file for flashing,
# linker script generation, partition_table generation, etc.
idf_build_executable(${elf_file})
