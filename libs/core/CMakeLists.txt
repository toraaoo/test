include(GNUInstallDirs)

add_library(hestia_core STATIC
        include/hestia/helper.h
        src/helper.cc
)

set_target_properties(hestia_core PROPERTIES OUTPUT_NAME "hestia")

target_include_directories(hestia_core
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
        src
)

find_package(spdlog REQUIRED)
target_link_libraries(hestia_core PRIVATE spdlog::spdlog)

target_compile_options(hestia_core PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

install(TARGETS hestia_core
        EXPORT hestia_coreTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})