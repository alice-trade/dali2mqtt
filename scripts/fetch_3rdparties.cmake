# esp-protocols
FetchContent_Declare(
        esp-protocols
        GIT_REPOSITORY https://github.com/espressif/esp-protocols.git
        GIT_SUBMODULES "ci" # no submodules
        GIT_SHALLOW    TRUE
)
    FetchContent_MakeAvailable(esp-protocols)
    set(ESP_PROTO_BASEDIR "${esp-protocols_SOURCE_DIR}/components")

# ArduinoJSON
FetchContent_Declare(
        arduinojson
        GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
        GIT_TAG        v7.4.2
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  "N/A"
)
    FetchContent_MakeAvailable(arduinojson)
    if(NOT TARGET ArduinoJson)
        add_library(arduinojson INTERFACE)
        target_include_directories(arduinojson SYSTEM INTERFACE
                "${arduinojson_SOURCE_DIR}/include"
        )
        add_library(ArduinoJson ALIAS arduinojson)
    endif()