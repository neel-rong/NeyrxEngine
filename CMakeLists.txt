cmake_minimum_required(VERSION 3.25)

project(
    NeyrxEngine
    VERSION 0.0.1
    LANGUAGES CXX
)

# C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# Build Configurations
set(
    CMAKE_CONFIGURATION_TYPES
    Debug
    Development
    Release
    CACHE STRING "Available build configurations"
    FORCE
)


# Build output directories
set(NEYRX_BUILD_DIR ${CMAKE_SOURCE_DIR}/Build)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${NEYRX_BUILD_DIR}/Bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${NEYRX_BUILD_DIR}/Bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${NEYRX_BUILD_DIR}/Lib)


# Engine
add_library(
    NeyrxCore
    Engine/Core/Private/Logger.cpp
)

target_include_directories(
    NeyrxCore
    PUBLIC
        Engine/Core/Public
)


# Compile Definitions
target_compile_definitions(
    NeyrxCore
    PUBLIC
        $<$<CONFIG:Debug>:NEYRX_ENABLE_LOGGING>
        $<$<CONFIG:Debug>:NEYRX_ENABLE_DEVELOPMENT>

        $<$<CONFIG:Development>:NEYRX_ENABLE_LOGGING>
        $<$<CONFIG:Development>:NEYRX_ENABLE_DEVELOPMENT>
)


# Sandbox
add_executable(
    NeyrxSandbox
    Sandbox/main.cpp
)

target_link_libraries(
    NeyrxSandbox
    PRIVATE
        NeyrxCore
)