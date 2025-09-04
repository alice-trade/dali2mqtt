cmake_minimum_required(VERSION 3.16)
project(DaliMQTTTests C CXX)

# --- 1. Подключение фреймворка ESP-IDF ---
include(../esp32/idf_buildup.cmake)
# --- 2. Подключение библиотеки DaliMQTT для тестирования ---
add_subdirectory(../src/driver daliC)
add_subdirectory(../src daliMQTTModules)

# --- 3. Определение тестового приложения ---
set(TEST_APP_NAME "daliMQTTTest")

# Рекурсивно находим все файлы тестов по шаблону *.test.c
file(GLOB_RECURSE TEST_CASE_FILES "*.test.c")

message(STATUS "Found test files: ${TEST_CASE_FILES}")

# Собираем исполняемый файл из точки входа и найденных тестов
add_executable(${TEST_APP_NAME}
        test_cases.c
        ${TEST_CASE_FILES}
)

# --- 4. Линковка библиотек ---
target_link_libraries(${TEST_APP_NAME} PRIVATE
        DaliMQTT
        idf::unity
)

# --- 5. Сборка исполняемого файла для ESP-IDF ---
idf_build_executable(${TEST_APP_NAME})

