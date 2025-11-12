# ESP32 Configuration CMAKE Lists
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR "CMAKE_TOOLCHAIN_FILE environment variable is not set. "
                "Please specify CMAKE_TOOLCHAIN_FILE explicitly. "
                "e.g., run 'get_idf' or '. $HOME/esp/esp-idf/export.sh'")
    endif ()

message(NOTICE "Using C compiler: " ${CMAKE_C_COMPILER})
message(NOTICE "IDF framework at: " $ENV{IDF_PATH})

set(ESP_BUILD_UTILS_PATH ${CMAKE_CURRENT_SOURCE_DIR})

include(idf_buildup.cmake)

set(app ${CMAKE_PROJECT_NAME})

add_subdirectory(../src/DaliMQTT daliMQTTModules)

include(../scripts/build_firmware.cmake)
include(../scripts/size_components.cmake)
include(../scripts/make_webui.cmake)

set(SUPPORTED_BUILD_TYPES "Release" "Debug")

if(NOT CMAKE_BUILD_TYPE IN_LIST SUPPORTED_BUILD_TYPES)
    message(WARNING "You are using an unsupported build type: ${CMAKE_BUILD_TYPE}")
endif()

option(BUILD_TESTS "Build the unit test executable" OFF)

if(BUILD_TESTS)
    message(STATUS "Test targets enabled. Use 'ninja test', 'test-flash', 'test-monitor'.")

    set(TESTS_BINARY_DIR ${CMAKE_BINARY_DIR}/tests_build)

    ExternalProject_Add(
            test
            SOURCE_DIR ${CMAKE_SOURCE_DIR}/tests
            BINARY_DIR ${TESTS_BINARY_DIR}

            CONFIGURE_COMMAND ${CMAKE_COMMAND}
            -G "${CMAKE_GENERATOR}"
            -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
            -DTARGET=${TARGET}
            -DIDF_TARGET=esp32s3
            -DBUILD_UNITY=1
            -DESP_BUILD_UTILS_PATH=${ESP_BUILD_UTILS_PATH}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            ${CMAKE_SOURCE_DIR}/tests

            BUILD_COMMAND ${CMAKE_MAKE_PROGRAM}

            TEST_COMMAND ""
            INSTALL_COMMAND ""

            USES_TERMINAL_CONFIGURE 1
            USES_TERMINAL_BUILD 1
    )

    add_custom_target(test-flash
            COMMAND ${CMAKE_MAKE_PROGRAM} -C ${TESTS_BINARY_DIR} flash
            DEPENDS test
            COMMENT "Flashing test firmware..."
            USES_TERMINAL
    )
    add_custom_target(test-menuconfig
            COMMAND ${CMAKE_MAKE_PROGRAM} -C ${TESTS_BINARY_DIR} menuconfig
            COMMENT "Setup sdkconfig for testing"
            USES_TERMINAL
    )
    add_custom_target(test-monitor
            COMMAND ${CMAKE_MAKE_PROGRAM} -C ${TESTS_BINARY_DIR} monitor
            COMMENT "Starting serial monitor for tests..."
            USES_TERMINAL
    )

else()
    message(STATUS "Test targets are disabled.")
endif()
message(STATUS "--------------------------------------------")
message(STATUS "Configuration done. Run ninja/make daliMQTT and ninja/make app-flash.")