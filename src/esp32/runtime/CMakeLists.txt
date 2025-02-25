# This file handles building each task as sub-project.
#
# Each task becomes an independent project in which you can experiment.
#
# Tasks are added as separate sub-projects so as compilation errors in
# one task don't affect another task.

project(DALIRuntime LANGUAGES CXX)

add_library(DALIRuntime STATIC
        "DALI_Runtime.cpp")

target_include_directories(DALIRuntime ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(DALIRuntime PRIVATE
        DALI
        idf::freertos
        idf::log
)