# ESP32 Configuration CMAKE Lists
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    # Проверяем, установлена ли переменная окружения IDF_PATH
    message(FATAL_ERROR "CMAKE_TOOLCHAIN_FILE environment variable is not set. "
                "Please specify CMAKE_TOOLCHAIN_FILE explicitly. "
                "e.g., run 'get_idf' or '. $HOME/esp/esp-idf/export.sh'")
    endif ()

message(NOTICE "Using C compiler: " ${CMAKE_C_COMPILER})
message(NOTICE "IDF framework at: " $ENV{IDF_PATH})



include(etc/idf_buildup.cmake)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(app ${CMAKE_PROJECT_NAME})

add_subdirectory(../src/esp32/driver/dalic dalic)
add_subdirectory(../src/esp32/modules daliMQTTModules)
add_subdirectory(../src/esp32/tests tests)

include(etc/build_firmware.cmake)
include(etc/size_components.cmake)

message(STATUS "Configuration done. Run ninja/make daliMQTT and ninja/make app-flash.")