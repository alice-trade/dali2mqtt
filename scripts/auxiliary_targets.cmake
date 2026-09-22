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
            "-DIRAM_ATTR="
            ${CPPCHECK_SUPPRESSIONS}
    )

    add_custom_target(cppcheck
            COMMAND ${CPPCHECK_EXE} ${CPPCHECK_ARGS}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Running static analysis with Cppcheck..."
            VERBATIM
    )
    message(STATUS "Cppcheck found: ${CPPCHECK_EXE}. Target 'cppcheck' enabled.")
endif()
# ---------------------

# clang-tidy
find_program(CLANG_TIDY_EXE NAMES "clang-tidy" "clang-tidy-18" "clang-tidy-17" "clang-tidy-16")
if(CLANG_TIDY_EXE)
    file(GLOB_RECURSE PROJECT_CXX_SOURCES
            "${CMAKE_SOURCE_DIR}/src/DaliMQTT/*.cxx"
            "${CMAKE_SOURCE_DIR}/src/main.cxx"
    )

    set(CLANG_TIDY_EXTRA_ARGS
            "--extra-arg=-Wno-unknown-warning-option"
            "--extra-arg=-Wno-unused-command-line-argument"
            "--extra-arg=-std=gnu++23"
    )

    set(CLANG_TIDY_COMMON_ARGS
            "-p=${CMAKE_BINARY_DIR}"
            "--header-filter=${CMAKE_SOURCE_DIR}/src/DaliMQTT/.*"
            ${CLANG_TIDY_EXTRA_ARGS}
    )

    add_custom_target(clang-tidy
            COMMAND ${CLANG_TIDY_EXE} ${CLANG_TIDY_COMMON_ARGS} ${PROJECT_CXX_SOURCES}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Running static analysis with Clang-Tidy..."
            USES_TERMINAL
            VERBATIM
    )

    add_custom_target(clang-tidy-autofix
            COMMAND ${CLANG_TIDY_EXE} ${CLANG_TIDY_COMMON_ARGS} -fix -fix-errors ${PROJECT_CXX_SOURCES}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Applying automatic fixes with Clang-Tidy..."
            USES_TERMINAL
            VERBATIM
    )
    message(STATUS "Clang-Tidy found: ${CLANG_TIDY_EXE}. Targets 'clang-tidy' and 'clang-tidy-autofix' enabled.")
endif()
# ---------------------


# Check IDF PY
find_program(IDF_PY_EXE NAMES "idf.py")
# ---------------------

# ESP-IDF Diagnostics
if(IDF_PY_EXE)
    add_custom_target(diag
            COMMAND ${IDF_PY_EXE} -B ${CMAKE_BINARY_DIR} diag
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Generating ESP-IDF diagnostic report directory..."
            USES_TERMINAL
    )
endif()
# ---------------------
