# --- Web UI Build & SPIFFS Image Generation ---

set(WEBUI_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/webui)
set(WEBUI_BUILD_DIR ${WEBUI_SOURCE_DIR}/dist)
set(SPIFFS_PARTITION_NAME "web_storage")

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

spiffs_create_partition_image(
        ${SPIFFS_PARTITION_NAME}
        ${WEBUI_BUILD_DIR}
        DEPENDS webui
)
add_dependencies(flash spiffs_${SPIFFS_PARTITION_NAME}_bin)

message(STATUS "Web UI build and SPIFFS image generation configured.")
