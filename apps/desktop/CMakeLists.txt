add_executable(hestia_desktop)

target_sources(hestia_desktop
        PRIVATE
        src/main.cc
)

target_link_libraries(hestia_desktop PRIVATE
        hestia_core
        fmt::fmt
        spdlog::spdlog
)

set_target_properties(hestia_desktop PROPERTIES OUTPUT_NAME "Hestia")

install(TARGETS hestia_desktop
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)