option(BUILD_TESTING "Build and configure testing targets" ON)

if(BUILD_TESTING)
    message(STATUS "Test targets enabled. Use 'ninja pytest-all', 'pytest-integration', 'test-flash', 'test-monitor'.")
    set(TESTS_BINARY_DIR ${CMAKE_BINARY_DIR}/tests_build)
    set(IDF_TARGET_TOOLCHAIN_FILE "$ENV{IDF_PATH}/tools/cmake/toolchain-${TARGET}.cmake")

    ExternalProject_Add(
            test_firmware_build
            SOURCE_DIR ${CMAKE_SOURCE_DIR}/tests
            BINARY_DIR ${TESTS_BINARY_DIR}

            CONFIGURE_COMMAND ${CMAKE_COMMAND}
            -G "${CMAKE_GENERATOR}"
            -DCMAKE_TOOLCHAIN_FILE=${IDF_TARGET_TOOLCHAIN_FILE}
            -DTARGET=${TARGET}
            -DIDF_TARGET=${TARGET}
            -DBUILD_UNITY=1
            -DESP_BUILD_UTILS_PATH=${ESP_BUILD_UTILS_PATH}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DPROJDIR=${PROJDIR}
            ${CMAKE_SOURCE_DIR}/tests

            BUILD_COMMAND ${CMAKE_MAKE_PROGRAM}

            BUILD_ALWAYS 1
            EXCLUDE_FROM_ALL 1

            TEST_COMMAND ""
            INSTALL_COMMAND ""

            USES_TERMINAL_CONFIGURE 1
            USES_TERMINAL_BUILD 1
    )

    add_custom_target(test-flash
            COMMAND ${CMAKE_MAKE_PROGRAM} -C ${TESTS_BINARY_DIR} flash
            DEPENDS test_firmware_build
            COMMENT "Flashing test firmware..."
            USES_TERMINAL
    )

    add_custom_target(test-monitor
            COMMAND ${CMAKE_MAKE_PROGRAM} -C ${TESTS_BINARY_DIR} monitor
            COMMENT "Starting serial monitor for test firmware..."
            USES_TERMINAL
    )
    find_program(PYTEST_EXE NAMES pytest pytest.exe)

    if(PYTEST_EXE)
        set(PYTEST_PORT_ARG "")
        if(EXISTS "/dev/ttyACM0")
            set(PYTEST_PORT_ARG "--port;/dev/ttyACM0")
        elseif(EXISTS "/dev/ttyUSB0")
            set(PYTEST_PORT_ARG "--port;/dev/ttyUSB0")
        endif()

        add_custom_target(pytest-unit
                COMMAND ${CMAKE_MAKE_PROGRAM} -C ${TESTS_BINARY_DIR} flash
                COMMAND ${PYTEST_EXE} ${CMAKE_SOURCE_DIR}/tests/test_embedded.py
                --target ${TARGET}
                --app-path ${TESTS_BINARY_DIR}
                ${PYTEST_PORT_ARG}
                DEPENDS test_firmware_build
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                COMMENT "Running Embedded Unit Tests via Pytest Embedded..."
                USES_TERMINAL
        )

        add_custom_target(pytest-integration
                COMMAND ${CMAKE_MAKE_PROGRAM} flash
                COMMAND ${PYTEST_EXE} ${CMAKE_SOURCE_DIR}/integration/test_main_app.py
                --target ${TARGET}
                --app-path ${CMAKE_BINARY_DIR}
                ${PYTEST_PORT_ARG}
                DEPENDS ${app} webui
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                COMMENT "Running Firmware Integration Tests via Pytest Embedded..."
                USES_TERMINAL
        )

        add_custom_target(pytest-all
                DEPENDS pytest-unit pytest-integration
                COMMENT "Running complete pytest suite (Embedded Unit + Integration)..."
        )
    else()
        message(WARNING "pytest not found. Pytest targets ('pytest-unit', 'pytest-integration') are disabled.")
    endif()

else()
    message(STATUS "Testing targets are disabled. Pass -DBUILD_TESTING=ON to enable.")
endif()
