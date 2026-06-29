add_executable(hestia_cli)

target_sources(hestia_cli
        PRIVATE
        src/main.cc
        src/cli.cc
        src/registry.cc
        src/commands/tui_command.cc
        src/commands/greet_command.cc
        src/commands/config_command.cc
        src/commands/autostart_command.cc
)

target_include_directories(hestia_cli PRIVATE src)

target_link_libraries(hestia_cli PRIVATE
        hestia_shared
        hestia_tui
        CLI11::CLI11
        spdlog::spdlog
)

set_target_properties(hestia_cli PROPERTIES OUTPUT_NAME "hestia")

install(TARGETS hestia_cli
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
