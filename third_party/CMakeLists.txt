add_subdirectory(fmt)
add_subdirectory(spdlog)

# CLI11: header-only command-line parser. Keep its tests/examples/docs and
# install rules out of our build.
set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(CLI11_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(CLI11)

# FTXUI: terminal UI library. Examples/docs/tests already default OFF; keep its
# install rules out of our build.
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(FTXUI)
