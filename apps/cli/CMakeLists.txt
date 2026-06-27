add_executable(hestia_cli)

target_sources(hestia_cli
        PRIVATE
        src/main.cc
)

target_link_libraries(hestia_cli PRIVATE
        hestia_core
        fmt::fmt
        spdlog::spdlog
)

set_target_properties(hestia_cli PROPERTIES OUTPUT_NAME "hestia")

install(TARGETS hestia_cli
        EXPORT hestia_cliTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)