# Dependency resolving,
# auto downloads using git either pass locally downloaded dependencies with definitions: "-D FETCHCONTENT_SOURCE_DIR_<DEPENDENCY>=<PATH>

# ETL
FetchContent_Declare(
        etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl.git
        GIT_TAG        20.49.0
        GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(etl)

# ArduinoJSON
FetchContent_Declare(
        arduinojson
        GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
        GIT_TAG        v7.4.3
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  "N/A"
)
FetchContent_MakeAvailable(arduinojson)
if(NOT TARGET ArduinoJson)
    add_library(ArduinoJson INTERFACE IMPORTED GLOBAL)
    target_include_directories(ArduinoJson SYSTEM INTERFACE
            "${arduinojson_SOURCE_DIR}/src"
    )
endif()

if(Dependencies_Platform_ESP)
    # esp-protocols
    FetchContent_Declare(
            esp-protocols
            GIT_REPOSITORY https://github.com/espressif/esp-protocols.git
            GIT_SUBMODULES "ci" # no submodules
            GIT_TAG 6057b2b19a0258b632ba3b29629cb14b894df4ab
    )
        FetchContent_MakeAvailable(esp-protocols)
        set(ESP_PROTO_BASEDIR "${esp-protocols_SOURCE_DIR}/components")

    # esp-mqtt
    FetchContent_Declare(
            esp-mqtt
            GIT_REPOSITORY https://github.com/espressif/esp-mqtt.git
            GIT_TAG        v1.1.0
            GIT_SHALLOW    TRUE
            SOURCE_SUBDIR  "N/A"
    )
        FetchContent_MakeAvailable(esp-mqtt)
endif()