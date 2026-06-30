add_library(hestia_tui STATIC
        # public surface — the ONLY public header
        include/hestia/tui/run.h

        # shell
        src/tui_app.cc
        src/app_context.h

        # navigation
        src/navigation/route.h
        src/navigation/view.h
        src/navigation/navigator.h
        src/navigation/navigator.cc
        src/navigation/view_registry.h
        src/navigation/view_registry.cc

        # layout system
        src/layout/layout.h
        src/layout/layout_registry.h
        src/layout/layout_registry.cc
        src/layout/layouts/sidebar_layout.h
        src/layout/layouts/sidebar_layout.cc
        src/layout/layouts/fullscreen_layout.h
        src/layout/layouts/fullscreen_layout.cc
        src/layout/layouts/centered_layout.h
        src/layout/layouts/centered_layout.cc
        src/layout/header_bar.h
        src/layout/header_bar.cc
        src/layout/status_bar.h
        src/layout/status_bar.cc
        src/layout/sidebar.h
        src/layout/sidebar.cc

        # reusable presentational components
        src/components/panel.h
        src/components/panel.cc
        src/components/key_hint.h
        src/components/key_hint.cc
        src/components/button.h
        src/components/button.cc
        src/components/text_field.h
        src/components/text_field.cc

        # input
        src/input/keymap.h
        src/input/global_keys.h
        src/input/global_keys.cc

        # theme
        src/theme/theme.h

        # views
        src/views/home_view.h
        src/views/home_view.cc
        src/views/settings_view.h
        src/views/settings_view.cc

        # overlays
        src/overlays/confirm_quit.h
        src/overlays/confirm_quit.cc
)

set_target_properties(hestia_tui PROPERTIES OUTPUT_NAME "hestia-tui")

# Public surface is exactly one header: hestia/tui/run.h. Everything under src/
# is private to the library.
target_include_directories(hestia_tui
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
        src
)

# Dependency rule: tui depends on the shared base ONLY (logging/identity), never
# the engine. ftxui/spdlog are implementation details — run.h exposes neither —
# so they are PRIVATE. As transitive link dependencies of this static library
# they still propagate to the final executable, which is why apps/cli no longer
# links ftxui directly.
target_link_libraries(hestia_tui
        PRIVATE
        hestia_shared
        ftxui::component
        spdlog::spdlog
)

target_compile_options(hestia_tui PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
