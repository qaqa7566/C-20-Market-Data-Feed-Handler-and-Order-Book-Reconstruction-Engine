cmake_minimum_required(VERSION 3.20)

project(mdfh
    VERSION 0.1.0
    DESCRIPTION "Market Data Feed Handler and Order Book Reconstruction Engine"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Build options
# ---------------------------------------------------------------------------
option(MDFH_BUILD_TESTS       "Build the GoogleTest suite"        ON)
option(MDFH_BUILD_BENCHMARKS  "Build benchmark executables"       ON)
option(MDFH_BUILD_APPS        "Build CLI applications"            ON)
option(MDFH_WARNINGS_AS_ERRORS "Treat warnings as errors"         OFF)
option(MDFH_ENABLE_ASAN_UBSAN "Enable Address+UB sanitizers"      OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    message(STATUS "No build type specified; defaulting to Release")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# ---------------------------------------------------------------------------
# Common compile flags (as an INTERFACE target so every component inherits them)
# ---------------------------------------------------------------------------
add_library(mdfh_flags INTERFACE)
target_compile_features(mdfh_flags INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(mdfh_flags INTERFACE /W4 /permissive-)
    if(MDFH_WARNINGS_AS_ERRORS)
        target_compile_options(mdfh_flags INTERFACE /WX)
    endif()
else()
    target_compile_options(mdfh_flags INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wcast-align -Wunused -Woverloaded-virtual -Wnon-virtual-dtor
        -Wdouble-promotion -Wimplicit-fallthrough)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # GCC 13 emits a false-positive -Wstringop-overflow inside libstdc++'s
        # std::vector reallocation memcpy at -O2/-O3 ("region of size 0").
        # Clang does not. Suppress only this one so -Werror stays useful.
        target_compile_options(mdfh_flags INTERFACE -Wno-stringop-overflow)
    endif()
    if(MDFH_WARNINGS_AS_ERRORS)
        target_compile_options(mdfh_flags INTERFACE -Werror)
    endif()
endif()

if(MDFH_ENABLE_ASAN_UBSAN AND NOT MSVC)
    message(STATUS "AddressSanitizer + UndefinedBehaviorSanitizer enabled")
    target_compile_options(mdfh_flags INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(mdfh_flags INTERFACE -fsanitize=address,undefined)
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_library(mdfh
    src/order_book.cpp
    src/book_builder.cpp
    src/simulator.cpp
    src/capture.cpp
    src/replay.cpp
    src/net/socket.cpp
    src/net/tcp.cpp
    src/net/udp.cpp)
target_include_directories(mdfh PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(mdfh PUBLIC mdfh_flags Threads::Threads)

# ---------------------------------------------------------------------------
# Applications
# ---------------------------------------------------------------------------
if(MDFH_BUILD_APPS)
    add_subdirectory(src/apps)
endif()

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------
if(MDFH_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(MDFH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
