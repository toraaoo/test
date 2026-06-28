add_executable(hestia_cli)

target_sources(hestia_cli
        PRIVATE
        src/main.cc
        src/tui.cc
)

target_link_libraries(hestia_cli PRIVATE
        hestia_core
        CLI11::CLI11
        ftxui::component
)

target_compile_definitions(hestia_cli PRIVATE
        HESTIA_VERSION="${PROJECT_VERSION}"
)

set_target_properties(hestia_cli PROPERTIES OUTPUT_NAME "hestia")

install(TARGETS hestia_cli
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
