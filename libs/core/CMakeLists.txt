include(GNUInstallDirs)

# Generate the shared identity header (name, id, vendor, version, channel).
configure_file(include/hestia/app_info.h.in
               "${CMAKE_CURRENT_BINARY_DIR}/include/hestia/app_info.h" @ONLY)

add_library(hestia_core STATIC
        include/hestia/logging.h
        include/hestia/greeting.h
        include/hestia/config.h
        src/logging.cc
        src/greeting.cc
        src/config.cc
)

set_target_properties(hestia_core PROPERTIES OUTPUT_NAME "hestia")

target_include_directories(hestia_core
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
        src
)

target_link_libraries(hestia_core PRIVATE
        spdlog::spdlog
        fmt::fmt
)

target_compile_options(hestia_core PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

install(TARGETS hestia_core
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/hestia/app_info.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/hestia")