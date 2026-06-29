include(GNUInstallDirs)

# Generate the shared identity header (name, id, vendor, version, channel).
configure_file(include/hestia/app_info.h.in
               "${CMAKE_CURRENT_BINARY_DIR}/include/hestia/app_info.h" @ONLY)

# hestia_shared — cross-cutting code linked by BOTH the daemon and every client
# (CLI/TUI/desktop): the IPC transport + protocol, the typed client SDK,
# application identity, and logging. It holds no launcher/engine logic — that
# lives in hestia_engine (daemon-only).
add_library(hestia_shared STATIC
        include/hestia/logging.h
        include/hestia/app_info.h.in
        include/hestia/ipc/transport.h
        include/hestia/ipc/endpoint.h
        include/hestia/ipc/protocol.h
        include/hestia/ipc/process.h
        include/hestia/ipc/process_codec.h
        include/hestia/ipc/errors.h
        include/hestia/ipc/topics.h
        include/hestia/client/client.h
        src/logging.cc
        src/endpoint.cc
        src/transport_posix.cc
        src/transport_windows.cc
        src/protocol.cc
        src/process_codec.cc
        src/client.cc
)

target_include_directories(hestia_shared
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# nlohmann/json is PUBLIC: it appears in protocol.h, so consumers that include
# the protocol envelope need its headers. spdlog/fmt back logging only and do
# not leak through the public headers, so they stay PRIVATE.
target_link_libraries(hestia_shared
        PUBLIC
        nlohmann_json::nlohmann_json
        PRIVATE
        spdlog::spdlog
        fmt::fmt
        Threads::Threads
)

target_compile_options(hestia_shared PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

install(TARGETS hestia_shared
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/hestia/app_info.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/hestia")
