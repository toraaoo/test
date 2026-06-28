# EXCLUDE_FROM_ALL: vendored deps build only when a first-party target links
# them, not as part of the default `all` target. So FTXUI/CLI11 are skipped
# entirely when the CLI (their only consumer) is not built.
add_subdirectory(fmt EXCLUDE_FROM_ALL)
add_subdirectory(spdlog EXCLUDE_FROM_ALL)

# CLI11: header-only command-line parser. Keep its tests/examples/docs and
# install rules out of our build.
set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(CLI11_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(CLI11 EXCLUDE_FROM_ALL)

# FTXUI: terminal UI library. Examples/docs/tests already default OFF; keep its
# install rules out of our build.
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(FTXUI EXCLUDE_FROM_ALL)
