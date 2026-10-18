cmake_minimum_required(VERSION 3.25)

# Universal binary: Max 8 on Apple silicon (8.2+) and Intel, and Max 9 inside Live 12.
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS architecture")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS")

include(FetchContent)
FetchContent_Declare(
    max-sdk-base
    GIT_REPOSITORY https://github.com/Cycling74/max-sdk-base.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(max-sdk-base)

project(NotaMax LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(MAX_SDK_CODESIGN_EXTERNS ON)   # ad-hoc sign: Max refuses unsigned externals on Apple silicon

# The Nota engine itself (plain C++, no JUCE).
add_library(nota_warnings INTERFACE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../source/dsp nota_dsp)

add_subdirectory(nota~)
