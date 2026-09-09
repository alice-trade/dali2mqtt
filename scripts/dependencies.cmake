# Dependency resolving,
# auto downloads using git either pass locally downloaded dependencies with definitions: "-D FETCHCONTENT_SOURCE_DIR_<DEPENDENCY>=<PATH>

# ETL
FetchContent_Declare(
        etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl.git
        GIT_TAG        20.48.1
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

if(Dependencies_Platform_HostTests)
    # Catch2
    FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG        v3.16.0
    )
    FetchContent_MakeAvailable(Catch2)
endif()

if(Dependencies_Platform_ESP)
    # esp-protocols
    FetchContent_Declare(
            esp-protocols
            GIT_REPOSITORY https://github.com/espressif/esp-protocols.git
            GIT_SUBMODULES "ci" # no submodules
            GIT_TAG da126db1f6e2b0c8df28de5112e5120e2215b8b7
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

    # esp-littlefs
    FetchContent_Declare(
            esp-littlefs
            GIT_REPOSITORY https://github.com/joltwallet/esp_littlefs.git
            GIT_TAG        v1.22.3
            GIT_SHALLOW    TRUE
            SOURCE_SUBDIR  "N/A"
    )
        FetchContent_MakeAvailable(esp-littlefs)
endif()