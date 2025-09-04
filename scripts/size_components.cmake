
set(idf_size ${python} -m esp_idf_size)
set(mapfile "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.map")

target_link_options(${app} PRIVATE "-Wl,--Map=${mapfile},--defsym=IDF_TARGET_${idf_target}=0")
add_custom_target(size-components
        COMMAND ${CMAKE_COMMAND}
        -D "IDF_SIZE_TOOL=${idf_size}"
        -D "IDF_SIZE_MODE=--archives"
        -D "MAP_FILE=${mapfile}"
        -D "OUTPUT_JSON=${OUTPUT_JSON}"
        -P "$ENV{IDF_PATH}/tools/cmake/run_size_tool.cmake"
        DEPENDS ${mapfile}
        USES_TERMINAL
        VERBATIM
)
