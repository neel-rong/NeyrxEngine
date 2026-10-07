cmake_minimum_required(VERSION 3.25)

# Build configurations
set(
    CMAKE_CONFIGURATION_TYPES
    Debug
    Development
    Release
    CACHE STRING "Available build configurations"
    FORCE
)

project(
    NeyrxEngine
    VERSION 0.0.1
    LANGUAGES CXX
)

# C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build output directories
set(NEYRX_BUILD_DIR ${CMAKE_SOURCE_DIR}/Build)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${NEYRX_BUILD_DIR}/Bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${NEYRX_BUILD_DIR}/Bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${NEYRX_BUILD_DIR}/Lib)

# Engine
add_subdirectory(Engine)

# Tests
add_subdirectory(Tests)

# Sandbox
add_subdirectory(Sandbox)