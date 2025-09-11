option(OFFLINE "Build dependencies with GIT, or prefer offline discover" OFF)

if(NOT OFFLINE)
    FetchContent_Declare(
            esp-protocols
            GIT_REPOSITORY https://github.com/espressif/esp-protocols.git
            GIT_SUBMODULES "ci" # no submodules
    )
    FetchContent_MakeAvailable(esp-protocols)
    set(ESP_PROTO_BASEDIR "${esp-protocols_SOURCE_DIR}/components")
else ()
    set(ESP_PROTO_BASEDIR $ENV{IDF_PATH}/../protocols/components)
endif ()