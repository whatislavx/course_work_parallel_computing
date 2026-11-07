cmake_minimum_required(VERSION 3.20)

project(search_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Sanitizer builds (TEST-DOC-09 / DoD): -DSEARCH_SANITIZER=thread | address | undefined | none
set(SEARCH_SANITIZER "none" CACHE STRING "Sanitizer: none, thread, address, undefined")
set_property(CACHE SEARCH_SANITIZER PROPERTY STRINGS none thread address undefined)

add_library(search_warnings INTERFACE)
target_compile_options(search_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)

if(NOT SEARCH_SANITIZER STREQUAL "none")
    target_compile_options(search_warnings INTERFACE -fsanitize=${SEARCH_SANITIZER} -fno-omit-frame-pointer)
    if(SEARCH_SANITIZER STREQUAL "thread")
        # GCC 13 видає -Wtsan для atomic_thread_fence у заголовках libstdc++; це не помилка коду проєкту.
        target_compile_options(search_warnings INTERFACE -Wno-tsan)
    endif()
    target_link_options(search_warnings INTERFACE -fsanitize=${SEARCH_SANITIZER})
endif()

option(SEARCH_BUILD_TESTS "Build GoogleTest suite" ON)

if(SEARCH_BUILD_TESTS)
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
endif()

add_subdirectory(src)
if(SEARCH_BUILD_TESTS)
    add_subdirectory(tests)
endif()
