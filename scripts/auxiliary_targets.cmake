# Auxiliary

# cppcheck
find_program(CPPCHECK_EXE NAMES "cppcheck")
if(CPPCHECK_EXE)
    set(CPPCHECK_FILTER "*/src/DaliMQTT/*")
    set(CPPCHECK_SUPP_FILE "${CMAKE_SOURCE_DIR}/cppcheck_suppressions.txt")

    set(CPPCHECK_ARGS
            "--project=${CMAKE_BINARY_DIR}/compile_commands.json"
            "--enable=warning,style,performance,portability"
            "--file-filter=${CPPCHECK_FILTER}"
            "--std=c++23"
            "--check-level=exhaustive"
            "--quiet"
            "--suppressions-list=${CPPCHECK_SUPP_FILE}"
            "--error-exitcode=1"
            "-DESP_LOGE(...)"
            "-DESP_LOGI(...)"
            "-DESP_LOGW(...)"
            "-DESP_LOGD(...)"
            "-DESP_LOGV(...)"
            "-Dconstexpr=const"
            "-DIRAM_ATTR="
            ${CPPCHECK_SUPPRESSIONS}
    )

    add_custom_target(cppcheck
            COMMAND ${CPPCHECK_EXE} ${CPPCHECK_ARGS}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Running static analysis with Cppcheck..."
            VERBATIM
    )
else()
    message(STATUS "Cppcheck not found. Target 'cppcheck' is disabled.")
endif()
# ---------------------