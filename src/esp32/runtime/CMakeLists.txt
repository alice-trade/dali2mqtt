# This file handles building each task as sub-project.
#
# Each task becomes an independent project in which you can experiment.
#
# Tasks are added as separate sub-projects so as compilation errors in
# one task don't affect another task.

project(DALIRuntime LANGUAGES C)

add_library(DALIRuntime STATIC
        "DALI_Runtime.c" "DALI_Runtime.h" )

target_link_libraries(DALIRuntime PRIVATE
        DALI
        idf::freertos
        idf::log
)