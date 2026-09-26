# Web UI Build

if(NOT CONFIG_DALI2MQTT_ENABLE_WEBUI)
    message(STATUS "WebUI is disabled.")
    return()
endif()

set(WEBUI_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/DaliMQTT/webui)
set(WEBUI_BUILD_DIR ${WEBUI_SOURCE_DIR}/dist)

find_program(NPM_EXECUTABLE NAMES npm npm.cmd)
if(NPM_EXECUTABLE)
    message(STATUS "Found npm: ${NPM_EXECUTABLE}")

    file(GLOB_RECURSE WEBUI_SRC_FILES
            "${WEBUI_SOURCE_DIR}/js/*"
            "${WEBUI_SOURCE_DIR}/index.html"
            "${WEBUI_SOURCE_DIR}/vite.config.ts"
            "${WEBUI_SOURCE_DIR}/vite.js"
    )

    add_custom_command(
            OUTPUT ${WEBUI_SOURCE_DIR}/node_modules/.uptodate_placeholder
            COMMAND ${NPM_EXECUTABLE} install
            COMMAND ${CMAKE_COMMAND} -E touch ${WEBUI_SOURCE_DIR}/node_modules/.uptodate_placeholder
            WORKING_DIRECTORY ${WEBUI_SOURCE_DIR}
            DEPENDS ${WEBUI_SOURCE_DIR}/package.json
            COMMENT "Installing Web UI dependencies..."
    )

    add_custom_command(
            OUTPUT ${WEBUI_BUILD_DIR}/index.html.gz
            ${WEBUI_BUILD_DIR}/app.js.gz
            ${WEBUI_BUILD_DIR}/app.css.gz
            COMMAND ${NPM_EXECUTABLE} run build
            WORKING_DIRECTORY ${WEBUI_SOURCE_DIR}
            DEPENDS ${WEBUI_SRC_FILES} ${WEBUI_SOURCE_DIR}/node_modules/.uptodate_placeholder
            COMMENT "Building and compressing Web UI..."
    )

    add_custom_target(webui_assets DEPENDS
            ${WEBUI_BUILD_DIR}/index.html.gz
            ${WEBUI_BUILD_DIR}/app.js.gz
            ${WEBUI_BUILD_DIR}/app.css.gz
    )

    target_add_binary_data(${app} "${WEBUI_BUILD_DIR}/index.html.gz" BINARY)
    target_add_binary_data(${app} "${WEBUI_BUILD_DIR}/app.js.gz" BINARY)
    target_add_binary_data(${app} "${WEBUI_BUILD_DIR}/app.css.gz" BINARY)

    add_dependencies(${app} webui_assets)
    message(STATUS "Web UI embedded Gzip assets configured.")

elseif(EXISTS "${WEBUI_BUILD_DIR}/index.html")
    message(WARNING "npm was not found, but pre-built Web UI '${WEBUI_BUILD_DIR}' exists. Using existing files.")

    target_add_binary_data(${app} "${WEBUI_BUILD_DIR}/index.html.gz" BINARY)
    target_add_binary_data(${app} "${WEBUI_BUILD_DIR}/app.js.gz" BINARY)
    target_add_binary_data(${app} "${WEBUI_BUILD_DIR}/app.css.gz" BINARY)
else()
    message(FATAL_ERROR
            "'npm' was not found.\n"
            "Install Node.js or provide pre-built assets in '${WEBUI_BUILD_DIR}'."
    )
endif()