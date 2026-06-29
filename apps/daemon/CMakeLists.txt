include(GNUInstallDirs)

# hestiad — the Hestia daemon. Owns the IPC endpoint, the request router, and
# (in later phases) the process supervisor and persisted process table. Links
# hestia_engine: the daemon is the only thing that links the engine directly.
add_executable(hestia_daemon
        src/main.cc
        src/router.cc
        src/router.h
        src/config_service.h
        src/process_supervisor.cc
        src/process_supervisor.h
        src/autostart.h
)

target_include_directories(hestia_daemon PRIVATE src)

target_link_libraries(hestia_daemon PRIVATE
        hestia_shared
        hestia_engine
        nlohmann_json::nlohmann_json
)

target_compile_options(hestia_daemon PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

set_target_properties(hestia_daemon PROPERTIES OUTPUT_NAME "hestiad")

install(TARGETS hestia_daemon
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
