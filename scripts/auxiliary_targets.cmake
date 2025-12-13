# Auxiliary

# DaliMQX build
set(DALIMQX_DIR "${PROJDIR}/src/DaliMQX")

add_custom_command(
        OUTPUT ${DALIMQX_DIR}/node_modules/.uptodate_placeholder
        COMMAND npm install
        COMMAND ${CMAKE_COMMAND} -E touch ${DALIMQX_DIR}/node_modules/.uptodate_placeholder
        WORKING_DIRECTORY ${DALIMQX_DIR}
        DEPENDS ${DALIMQX_DIR}/package.json
        COMMENT "Installing DaliMQX dependencies..."
        VERBATIM
)
file(GLOB_RECURSE DALIMQX_SOURCES "${DALIMQX_DIR}/*.ts" "${DALIMQX_DIR}/*.json")

add_custom_command(
        OUTPUT ${DALIMQX_DIR}/dist/dalimqx.browser.min.js
        COMMAND npm run build
        WORKING_DIRECTORY ${DALIMQX_DIR}
        DEPENDS
        ${DALIMQX_DIR}/node_modules/.uptodate_placeholder
        ${DALIMQX_SOURCES}
        COMMENT "Building DaliMQX library..."
        VERBATIM
)
add_custom_target(daliMQX ALL
        DEPENDS ${DALIMQX_DIR}/dist/dalimqx.browser.min.js
)