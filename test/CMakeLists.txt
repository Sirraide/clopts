cmake_minimum_required(VERSION 3.21)
project(clopts VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR})

add_library(options INTERFACE)
target_compile_options(options INTERFACE
        -Wall -Wextra -Wundef -Werror=return-type -Wconversion -Wpedantic
        -fdiagnostics-color=always $<$<CXX_COMPILER_ID:Clang>:-fcolor-diagnostics>)
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(options INTERFACE -O3)
else ()
    target_compile_options(options INTERFACE -O0 -ggdb)
endif ()

add_executable(test test.cc ../include/clopts.hh)
target_link_libraries(test PRIVATE options)
