include(GNUInstallDirs)

# hestia-tray — the resident system-tray helper. A thin client: it links
# hestia_shared (transport + client SDK) and a platform GUI backend, never the
# engine. The backend is the only GUI-toolkit dependency in the project outside
# the desktop app, kept here so the CLI stays headless-safe.

set(tray_backend "")
if (WIN32)
    set(tray_backend src/backend_windows.cc)
elseif (APPLE)
    set(tray_backend src/backend_macos.mm)
else ()
    # Linux: StatusNotifierItem + dbusmenu spoken directly over GDBus (gio-2.0) —
    # no GUI toolkit. gio is universally packaged (libglib2.0-dev), so this builds
    # on any CI runner. If it is somehow absent, skip the target rather than fail
    # the whole build — the daemon, CLI, and desktop app do not depend on the tray.
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(GIO QUIET gio-2.0)
    endif ()
    if (NOT GIO_FOUND)
        message(STATUS "hestia-tray: gio-2.0 not found; skipping tray helper")
        return ()
    endif ()
    set(tray_backend src/backend_linux.cc)
endif ()

add_executable(hestia_tray
        src/main.cc
        src/tray_app.cc
        src/tray_app.h
        src/tray_backend.h
        ${tray_backend}
)

target_include_directories(hestia_tray PRIVATE src)

target_link_libraries(hestia_tray PRIVATE
        hestia_shared
        Threads::Threads
)

if (WIN32)
    target_link_libraries(hestia_tray PRIVATE shell32 user32)
elseif (APPLE)
    set_source_files_properties(src/backend_macos.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(hestia_tray PRIVATE "-framework Cocoa")
else ()
    target_include_directories(hestia_tray PRIVATE ${GIO_INCLUDE_DIRS})
    target_link_libraries(hestia_tray PRIVATE ${GIO_LIBRARIES})
    target_compile_options(hestia_tray PRIVATE ${GIO_CFLAGS_OTHER})
endif ()

# GLib's function-like macros expand in our translation units, so -Wpedantic is
# dropped here (it is kept on the engine/daemon, which have no toolkit headers).
target_compile_options(hestia_tray PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

set_target_properties(hestia_tray PROPERTIES OUTPUT_NAME "hestia-tray")

install(TARGETS hestia_tray
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
