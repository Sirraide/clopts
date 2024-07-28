cmake_minimum_required(VERSION 3.21)
project(clopts VERSION 2.2.0)

set(CMAKE_CXX_STANDARD 20)

Include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.4.0
)

FetchContent_MakeAvailable(Catch2)

add_executable(tests test.cc ../include/clopts.hh)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_executable(fuzz fuzz.cc ../include/clopts.hh)
    target_compile_options(fuzz PRIVATE
        $<$<CONFIG:DEBUG>:-O0 -g3 -ggdb3>
        $<$<CONFIG:RELEASE>:-O3 -march=native>
        -fsanitize=fuzzer,address,undefined
    )
    target_link_options(fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

if (NOT MSVC)
    target_compile_options(tests PRIVATE
        -Wall -Wextra -Werror
        $<$<CONFIG:DEBUG>:-O0 -g3 -ggdb3 -fsanitize=address>
        $<$<CONFIG:RELEASE>:-O3 -march=native>
    )
    target_link_options(tests PRIVATE
        $<$<CONFIG:DEBUG>:-O0 -g3 -ggdb3 -rdynamic -fsanitize=address>
        $<$<CONFIG:RELEASE>:-O3 -march=native>
    )
endif()

if (NOT WIN32)
    target_link_libraries(tests PRIVATE m)
endif()
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(tests)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_test(
        NAME static-assert-errors
        COMMAND "${CMAKE_CXX_COMPILER}"
            -Xclang -verify
            -std=c++26
            -fsyntax-only
            -ferror-limit=0
            "${PROJECT_SOURCE_DIR}/errors.cc"
            -isystem "${PROJECT_SOURCE_DIR}/../include"
    )
endif()