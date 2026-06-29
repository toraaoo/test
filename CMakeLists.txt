cmake_minimum_required(VERSION 3.21)

project(hestia
        VERSION 0.0.1
        LANGUAGES C CXX
)

if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# The daemon supervises on background threads and the client SDK runs a reader
# thread, so both link the platform threads library.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# ---- Shared application identity (consumed by hestia_shared + desktop) ------
set(APP_NAME    "Hestia"                    CACHE STRING "Human-facing product name")
set(APP_ID      "tech.lawrenceallen.hestia" CACHE STRING "Reverse-DNS application id")
set(APP_VENDOR  "toraaoo"                   CACHE STRING "Vendor / maintainer")
set(APP_CHANNEL "dev"                       CACHE STRING "Release channel")
# APP_VERSION flows from project(hestia VERSION ...) above.

add_subdirectory(third_party)
add_subdirectory(libs/shared)
add_subdirectory(libs/engine)
add_subdirectory(libs/tui)
add_subdirectory(apps/cli)
add_subdirectory(apps/desktop)
add_subdirectory(apps/daemon)
