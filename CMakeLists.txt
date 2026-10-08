cmake_minimum_required(VERSION 3.22)

# CMake may itself be an x86_64 binary running under Rosetta, in which case it would
# build x86_64 on an Apple-silicon Mac (and native hosts could not load the plugin).
# Unless told otherwise, build for the machine's native architecture.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    execute_process(COMMAND sysctl -n hw.optional.arm64
        OUTPUT_VARIABLE NOTA_HAS_ARM64 OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOTA_HAS_ARM64 STREQUAL "1")
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures for macOS" FORCE)
    endif()
endif()

project(Nota VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment target")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(NOTA_BUILD_PLUGIN "Build the JUCE plugin (VST3/AU/Standalone); OFF builds only the DSP, tools and tests" ON)

enable_testing()

add_library(nota_warnings INTERFACE)
if(MSVC)
    target_compile_options(nota_warnings INTERFACE /W4)
else()
    target_compile_options(nota_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow)
endif()

# -----------------------------------------------------------------------------
# DSP core (plain C++17, no JUCE)
# -----------------------------------------------------------------------------
add_subdirectory(source/dsp)

# -----------------------------------------------------------------------------
# Offline tools and tests (link only nota_dsp)
# -----------------------------------------------------------------------------
add_executable(nota_bench tools/nota_bench.cpp)
target_link_libraries(nota_bench PRIVATE nota_dsp nota_warnings)
target_include_directories(nota_bench PRIVATE tools)

add_executable(nota_eval tools/nota_eval.cpp)
target_link_libraries(nota_eval PRIVATE nota_dsp nota_warnings)
target_include_directories(nota_eval PRIVATE tools)

add_executable(nota_cli tools/nota_cli.cpp)
target_link_libraries(nota_cli PRIVATE nota_dsp nota_warnings)
target_include_directories(nota_cli PRIVATE tools)

add_executable(nota_tests tests/nota_tests.cpp)
target_link_libraries(nota_tests PRIVATE nota_dsp nota_warnings)
target_include_directories(nota_tests PRIVATE tools)

add_test(NAME nota_tests COMMAND nota_tests)
set_tests_properties(nota_tests PROPERTIES TIMEOUT 300)

# -----------------------------------------------------------------------------
# Plugin
# -----------------------------------------------------------------------------
if(NOTA_BUILD_PLUGIN)
    add_subdirectory(source/plugin)
endif()
