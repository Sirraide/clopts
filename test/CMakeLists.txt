cmake_minimum_required(VERSION 3.21)
project(clopts VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 20)

Include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.4.0
)

FetchContent_MakeAvailable(Catch2)

add_executable(tests test.cc ../include/clopts.hh)

target_compile_options(tests PRIVATE -Wall -Wextra -Werror)
target_compile_options(tests PRIVATE
    $<$<CONFIG:DEBUG>:-O0 -g3 -ggdb3 -rdynamic -fsanitize=address>
    $<$<CONFIG:RELEASE>:-O3 -march=native>
)
target_link_options(tests PRIVATE
    $<$<CONFIG:DEBUG>:-O0 -g3 -ggdb3 -rdynamic -fsanitize=address>
    $<$<CONFIG:RELEASE>:-O3 -march=native>
)

target_link_libraries(tests PRIVATE Catch2::Catch2WithMain m)
set_target_properties(tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR})

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(tests)
