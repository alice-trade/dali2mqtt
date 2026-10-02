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
            "--extra-arg=-std=gnu++2b"
            "--extra-arg=-march=rv32imac"
            "--extra-arg=-fno-builtin"
            "--extra-arg=-D__clang_analyzer__"
            "--extra-arg=-Xclang"
            "--extra-arg=-detailed-preprocessing-record"
    )

    if(CMAKE_CXX_COMPILER)
        execute_process(
                COMMAND ${CMAKE_CXX_COMPILER} -print-sysroot
                OUTPUT_VARIABLE GCC_SYSROOT
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
        )
        if(GCC_SYSROOT)
            list(APPEND CLANG_TIDY_EXTRA_ARGS "--extra-arg=--sysroot=${GCC_SYSROOT}")
        endif()
    endif()

    set(CLANG_TIDY_COMMON_ARGS
            "-p=${CMAKE_BINARY_DIR}"
            "--header-filter=${CMAKE_SOURCE_DIR}/src/DaliMQTT/.*"
            ${CLANG_TIDY_EXTRA_ARGS}
    )
    set(CLANGTIDY_REQUIRED_PCH_FILE "${CMAKE_BINARY_DIR}/src/DaliMQTT/CMakeFiles/DaliMQTT-Core.dir/cmake_pch.hxx.gch")

    add_custom_target(clang-tidy
            COMMAND ${CMAKE_COMMAND} -E rename "${CLANGTIDY_REQUIRED_PCH_FILE}" "${CLANGTIDY_REQUIRED_PCH_FILE}.bak"
            COMMAND ${CLANG_TIDY_EXE} ${CLANG_TIDY_COMMON_ARGS} ${PROJECT_CXX_SOURCES}
            COMMAND ${CMAKE_COMMAND} -E rename "${CLANGTIDY_REQUIRED_PCH_FILE}.bak" "${CLANGTIDY_REQUIRED_PCH_FILE}"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Running static analysis with Clang-Tidy..."
            USES_TERMINAL
            VERBATIM
    )
    message(STATUS "Clang-Tidy found: ${CLANG_TIDY_EXE}. Targets enabled.")
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
