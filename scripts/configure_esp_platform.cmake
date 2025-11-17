if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR "CMAKE_TOOLCHAIN_FILE environment variable is not set. "
            "Please specify CMAKE_TOOLCHAIN_FILE explicitly. "
            "e.g., run 'get_idf' or '. $HOME/esp/esp-idf/export.sh'")
endif ()

message(NOTICE "Using C/C++ compiler: " ${CMAKE_CXX_COMPILER})
message(NOTICE "Using Build Tool: " ${CMAKE_BUILD_TOOL})
message(NOTICE "IDF framework at: " $ENV{IDF_PATH})

set(SUPPORTED_BUILD_TYPES "Release" "Debug")

if(NOT CMAKE_BUILD_TYPE IN_LIST SUPPORTED_BUILD_TYPES)
    message(WARNING "You are using an unsupported build type: ${CMAKE_BUILD_TYPE}")
endif()

set(ESP_BUILD_UTILS_PATH ${CMAKE_CURRENT_SOURCE_DIR})

include(../scripts/idf_buildup.cmake)

set(app ${CMAKE_PROJECT_NAME})

add_subdirectory(../src/DaliMQTT daliMQTTModules)

include(../scripts/build_firmware.cmake)
include(../scripts/size_components.cmake)
include(../scripts/make_webui.cmake)

include(../scripts/build_tests.cmake)

message(NOTICE "--------------------------------------------")
message(STATUS "Configuration done. Run ninja/make daliMQTT and ninja/make flash")