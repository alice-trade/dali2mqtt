# --- Web UI Build & SPIFFS Image Generation ---

set(WEBUI_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/webui)
set(WEBUI_BUILD_DIR ${WEBUI_SOURCE_DIR}/dist)
set(SPIFFS_IMAGE ${CMAKE_BINARY_DIR}/spiffs.img)
set(SPIFFS_PARTITION_LABEL "web_storage")

add_custom_command(
        OUTPUT ${WEBUI_SOURCE_DIR}/node_modules
        COMMAND npm install
        WORKING_DIRECTORY ${WEBUI_SOURCE_DIR}
        COMMENT "Installing Web UI dependencies..."
)

add_custom_target(webui ALL
        COMMAND npm run build
        WORKING_DIRECTORY ${WEBUI_SOURCE_DIR}
        DEPENDS ${WEBUI_SOURCE_DIR}/node_modules
        COMMENT "Building Web UI..."
)

add_custom_command(
        OUTPUT ${SPIFFS_IMAGE}
        COMMAND ${PYTHON} ${IDF_PATH}/components/spiffs/spiffsgen.py
        ${PROJECT_TOTAL_SIZE} ${WEBUI_STORAGE_DIR} ${SPIFFS_IMAGE}
        DEPENDS webui_build
        COMMENT "Generating SPIFFS image from ${WEBUI_STORAGE_DIR}..."
        VERBATIM
)

add_custom_target(spiffs_img DEPENDS ${SPIFFS_IMAGE})

idf_build_get_property(flash_args FLASH_ARGS)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/esp32/partitions.csv")

idf_component_get_property(partition_table partition_table COMPONENT "partition_table")
list(GET partition_table 0 partition_table_offset)

list(APPEND flash_args
        ${partition_table_offset} ${SPIFFS_IMAGE}
)
idf_build_set_property(FLASH_ARGS "${flash_args}" APPEND)

message(STATUS "Web UI build and SPIFFS image generation configured.")
message(STATUS "Run 'ninja spiffs_img' to build image, 'ninja flash' or 'ninja app-flash' will flash it.")