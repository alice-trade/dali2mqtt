# This file handles building each task as sub-project.
#
# Each task becomes an independent project in which you can experiment.
#
# Tasks are added as separate sub-projects so as compilation errors in
# one task don't affect another task.

cmake_minimum_required(VERSION 3.16)

project(daliMQTTTest)

set(TESTSRC )
add_executable(daliMQTTTest.elf TESTSRC)

target_link_libraries(daliMQTTTest.elf idf::freertos idf::cxx idf::mqtt idf::driver idf::esp_eth idf::newlib idf::unity)
