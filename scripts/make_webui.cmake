# Web UI Build & LittleFS Image Generation

set(WEBUI_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/DaliMQTT/webui)
set(WEBUI_BUILD_DIR ${WEBUI_SOURCE_DIR}/dist)
set(LITTLEFS_PARTITION_NAME "web_storage")

find_program(NPM_EXECUTABLE NAMES npm npm.cmd)
if(NPM_EXECUTABLE)
    message(STATUS "Found npm: ${NPM_EXECUTABLE}")

    add_custom_command(
            OUTPUT ${WEBUI_SOURCE_DIR}/node_modules/.uptodate_placeholder
            COMMAND ${NPM_EXECUTABLE} install
            COMMAND ${CMAKE_COMMAND} -E touch ${WEBUI_SOURCE_DIR}/node_modules/.uptodate_placeholder
            WORKING_DIRECTORY ${WEBUI_SOURCE_DIR}
            DEPENDS ${WEBUI_SOURCE_DIR}/package.json ${WEBUI_SOURCE_DIR}/package-lock.json
            COMMENT "Installing Web UI dependencies..."
    )

    add_custom_target(webui
            COMMAND ${NPM_EXECUTABLE} run build
            WORKING_DIRECTORY ${WEBUI_SOURCE_DIR}
            DEPENDS ${WEBUI_SOURCE_DIR}/node_modules/.uptodate_placeholder
            COMMENT "Building Web UI..."
    )

    littlefs_create_partition_image(
            ${LITTLEFS_PARTITION_NAME}
            ${WEBUI_BUILD_DIR}
            DEPENDS webui
    )
    add_dependencies(flash ${LITTLEFS_PARTITION_NAME}-flash)

    message(STATUS "Web UI build and LittleFS image generation configured.")

elseif(EXISTS "${WEBUI_BUILD_DIR}/index.html")
    message(WARNING "npm was not found, but pre-built Web UI '${WEBUI_BUILD_DIR}' exists. Using existing files.")

    littlefs_create_partition_image(
            ${LITTLEFS_PARTITION_NAME}
            ${WEBUI_BUILD_DIR}
    )
    add_dependencies(flash ${LITTLEFS_PARTITION_NAME}-flash)

else()
    message(FATAL_ERROR
            "'npm' was not found.\n"
            "Install Node.js or provide pre-built assets in '${WEBUI_BUILD_DIR}'."
    )
endif()