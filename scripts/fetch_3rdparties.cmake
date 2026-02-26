# esp-protocols
FetchContent_Declare(
        esp-protocols
        GIT_REPOSITORY https://github.com/espressif/esp-protocols.git
        GIT_SUBMODULES "ci" # no submodules
        GIT_SHALLOW    TRUE
)
    FetchContent_MakeAvailable(esp-protocols)
    set(ESP_PROTO_BASEDIR "${esp-protocols_SOURCE_DIR}/components")

# ETL
FetchContent_Declare(
        etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl.git
        GIT_TAG        20.45.0
        GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(etl)

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
                "${arduinojson_SOURCE_DIR}/src"
        )
        add_library(ArduinoJson ALIAS arduinojson)
    endif()