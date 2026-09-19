include(ExternalProject)
include(FetchContent)

find_package(Git)
find_program(IDF_PY_EXE NAMES "idf.py")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
