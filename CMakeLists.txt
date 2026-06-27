cmake_minimum_required(VERSION 3.21)

project(hestia
        VERSION 0.0.1
        LANGUAGES CXX
)

if (CMAKE_CONFIGURATION_TYPES)
    # Visual Studio, Xcode, Ninja Multi-Config
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
else ()
    # Ninja, Unix Makefiles
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
)

add_subdirectory(third_party)
add_subdirectory(libs/core)
add_subdirectory(apps/desktop)

option(BUILD_CLI "Build the command-line interface (CLI) application" ON)

if (BUILD_CLI)
    add_subdirectory(apps/cli)
endif ()
