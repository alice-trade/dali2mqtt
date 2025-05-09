# ESP32 Configuration CMAKE Lists
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(STATUS "CMAKE_TOOLCHAIN_FILE is not set. Attempting to use default ESP-IDF toolchain.")

    # Проверяем, установлена ли переменная окружения IDF_PATH
    if(NOT DEFINED ENV{IDF_PATH})
        message(FATAL_ERROR "IDF_PATH environment variable is not set. "
                "Please set it or specify CMAKE_TOOLCHAIN_FILE explicitly. "
                "e.g., run 'get_idf' or '. $HOME/esp/esp-idf/export.sh'")
    else()
        set(CMAKE_TOOLCHAIN_FILE  "$ENV{IDF_PATH}/tools/cmake/toolchain-$ENV{IDF_TARGET}.cmake")
    endif ()
endif ()

message(NOTICE "Using C compiler: " ${CMAKE_C_COMPILER})
message(NOTICE " with IDF framework at: " $ENV{IDF_PATH})



include(etc/idf_buildup.cmake)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(app ${CMAKE_PROJECT_NAME})

add_subdirectory(../src/esp32/driver/dalic dalic)
add_subdirectory(../src/esp32/modules daliMQTTModules)

include(etc/build_firmware.cmake)
include(etc/make_esp_test_target.cmake)
include(etc/size_components.cmake)

message(STATUS "Configuration done. Run ninja <target>.")