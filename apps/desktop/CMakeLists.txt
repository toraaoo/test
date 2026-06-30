include(GNUInstallDirs)

# ---- Platform / arch detection -----------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(CEF_PLATFORM "linuxarm64")
    else()
        set(CEF_PLATFORM "linux64")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CEF_PLATFORM "windows64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        set(CEF_PLATFORM "macosarm64")
    else()
        set(CEF_PLATFORM "macosx64")
    endif()
endif()

include(cef_version)

include(DownloadCEF)
DownloadCEF("${CEF_PLATFORM}" "" "${CEF_VERSION}" "${CMAKE_SOURCE_DIR}/third_party/cef")
list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")

# Sandbox policy: off in Debug, on in Release. Set BEFORE find_package(CEF).
if(NOT DEFINED USE_SANDBOX)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(USE_SANDBOX OFF CACHE BOOL "Enable the CEF sandbox")
    else()
        set(USE_SANDBOX ON  CACHE BOOL "Enable the CEF sandbox")
    endif()
endif()

find_package(CEF REQUIRED)

# CEF compiles with exceptions and RTTI disabled and propagates those flags to
# every target (SET_*_TARGET_PROPERTIES). Our own code — and spdlog and the
# throwing client SDK — need them, so strip the disabling flags from CEF's
# variables here, before any target (including the wrapper below) consumes them.
list(REMOVE_ITEM CEF_CXX_COMPILER_FLAGS -fno-exceptions -fno-rtti)
list(REMOVE_ITEM CEF_COMPILER_FLAGS /GR-)
list(REMOVE_ITEM CEF_COMPILER_DEFINES _HAS_EXCEPTIONS=0)
if(MSVC)
    list(APPEND CEF_CXX_COMPILER_FLAGS /EHsc)
endif()

# Per-config output dir CEF expects.
if(CMAKE_CONFIGURATION_TYPES)
    set(CEF_TARGET_OUT_DIR "${CMAKE_BINARY_DIR}/$<CONFIG>")
else()
    set(CEF_TARGET_OUT_DIR "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
endif()

# Build the DLL wrapper that CEF ships as source.
add_subdirectory("${CEF_ROOT}/libcef_dll" libcef_dll_wrapper)

# Imported libcef (not needed on macOS — loaded at runtime).
if(WIN32)
    add_library(libcef_lib SHARED IMPORTED)
    set_target_properties(libcef_lib PROPERTIES
        IMPORTED_LOCATION "${CEF_ROOT}/Release/libcef.dll"
        IMPORTED_IMPLIB   "${CEF_ROOT}/Release/libcef.lib")
elseif(UNIX AND NOT APPLE)
    add_library(libcef_lib SHARED IMPORTED)
    set_target_properties(libcef_lib PROPERTIES
        IMPORTED_LOCATION "${CEF_ROOT}/Release/libcef.so")
endif()

# ---- Application identity ---------------------------------------------------
# On-disk executable name. On case-insensitive filesystems (Windows) a "Hestia"
# binary would collide with the "hestia" CLI, so off macOS the launcher is named
# "HestiaLauncher". The window/app *identity* stays "Hestia" via APP_NAME/APP_ID
# — only the filename differs. macOS keeps the "Hestia.app" bundle name.
if(APPLE)
    set(_app_default_binary_name "Hestia")
else()
    set(_app_default_binary_name "HestiaLauncher")
endif()
set(APP_BINARY_NAME "${_app_default_binary_name}" CACHE STRING "Output binary / bundle name")
set(APP_SCHEME      "hestia" CACHE STRING "Custom URL scheme serving the frontend")
set(APP_DEV_SERVER_URL ""    CACHE STRING "Dev-server URL (Debug only; empty = embedded)")

configure_file(src/core/build_config.h.in
               "${CMAKE_CURRENT_BINARY_DIR}/generated/core/build_config.h" @ONLY)

# ---- Embedded frontend (CMakeRC) --------------------------------------------
include(CMakeRC)

set(FRONTEND_DIST "${CMAKE_CURRENT_SOURCE_DIR}/frontend/dist")
file(GLOB_RECURSE FRONTEND_ASSETS CONFIGURE_DEPENDS "${FRONTEND_DIST}/*")
if(NOT FRONTEND_ASSETS)
    message(FATAL_ERROR
        "No frontend build found at ${FRONTEND_DIST}.\n"
        "Build it first:  (cd apps/desktop/frontend && bun install && bun run build)")
endif()

cmrc_add_resource_library(
    hestia_frontend
    ALIAS     hestia::frontend
    NAMESPACE hestia_frontend
    WHENCE    "${FRONTEND_DIST}"
    ${FRONTEND_ASSETS})

# ---- Common sources (every platform) ----------------------------------------
set(DESKTOP_SRCS_COMMON
    src/core/app/app_base.cc
    src/core/app/browser_app.cc
    src/core/app/renderer_app.cc
    src/core/app/main_util.cc
    src/core/app/tray_launcher.cc
    src/core/browser/client.cc
    src/core/browser/client_manager.cc
    src/core/common/app_scheme.cc
    src/core/common/app_settings.cc
    src/core/daemon/daemon_client.cc
    src/core/ipc/ipc_router.cc
    src/core/window/window_delegate.cc
    src/core/window/window_util.cc
    src/features/feature_registry.cc
    src/features/app/app_feature.cc
    src/features/settings/settings_feature.cc)

# ---- Platform targets -------------------------------------------------------
if(APPLE)
    # macOS: .app bundle + one helper bundle per CEF sub-process.
    add_executable(hestia_desktop MACOSX_BUNDLE
        ${DESKTOP_SRCS_COMMON}
        src/main_mac.mm
        src/core/browser/client_mac.mm)
    SET_EXECUTABLE_TARGET_PROPERTIES(hestia_desktop)
    set_target_properties(hestia_desktop PROPERTIES
        OUTPUT_NAME "${APP_BINARY_NAME}"
        MACOSX_BUNDLE_INFO_PLIST
            "${CMAKE_CURRENT_SOURCE_DIR}/src/resources/macos/Info.plist.in")
    target_include_directories(hestia_desktop PRIVATE
        ${CEF_INCLUDE_PATH} src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(hestia_desktop PRIVATE
        hestia_shared hestia::frontend
        libcef_dll_wrapper ${CEF_STANDARD_LIBS}
        fmt::fmt spdlog::spdlog)
    if(USE_SANDBOX)
        target_link_libraries(hestia_desktop PRIVATE
            "${CEF_ROOT}/$<CONFIG>/cef_sandbox.a")
    endif()

    set(CEF_APP "${CEF_TARGET_OUT_DIR}/${APP_BINARY_NAME}.app")
    COPY_MAC_FRAMEWORK(hestia_desktop "${CEF_BINARY_DIR}" "${CEF_APP}")

    foreach(_suffix ${CEF_HELPER_APP_SUFFIXES})
        string(REPLACE ":" ";" _parts "${_suffix}")
        list(GET _parts 0 _name)
        list(GET _parts 1 _tgt)
        list(GET _parts 2 _pl)
        set(_helper "hestia_helper${_tgt}")
        set(_helper_name "${APP_NAME} Helper${_name}")
        set(HELPER_NAME        "${_helper_name}")
        set(HELPER_BUNDLE_ID   "${APP_ID}${_pl}")
        configure_file(
            src/resources/macos/helper-Info.plist.in
            "${CMAKE_CURRENT_BINARY_DIR}/${_helper}-Info.plist" @ONLY)
        add_executable(${_helper} MACOSX_BUNDLE src/process_helper_mac.cc)
        SET_EXECUTABLE_TARGET_PROPERTIES(${_helper})
        target_include_directories(${_helper} PRIVATE ${CEF_INCLUDE_PATH} src)
        target_link_libraries(${_helper} PRIVATE libcef_dll_wrapper ${CEF_STANDARD_LIBS})
        if(USE_SANDBOX)
            target_link_libraries(${_helper} PRIVATE "${CEF_ROOT}/$<CONFIG>/cef_sandbox.a")
        endif()
        set_target_properties(${_helper} PROPERTIES
            OUTPUT_NAME "${_helper_name}"
            MACOSX_BUNDLE_INFO_PLIST
                "${CMAKE_CURRENT_BINARY_DIR}/${_helper}-Info.plist")
        add_dependencies(hestia_desktop ${_helper})
        add_custom_command(TARGET hestia_desktop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CEF_TARGET_OUT_DIR}/${_helper_name}.app"
                "${CEF_APP}/Contents/Frameworks/${_helper_name}.app" VERBATIM)
    endforeach()

else()
    # Linux / Windows
    if(WIN32 AND USE_SANDBOX)
        add_library(hestia_desktop SHARED
            ${DESKTOP_SRCS_COMMON} src/main_win.cc src/core/browser/client_win.cc)
        SET_LIBRARY_TARGET_PROPERTIES(hestia_desktop)
    elseif(WIN32)
        add_executable(hestia_desktop WIN32
            ${DESKTOP_SRCS_COMMON} src/main_win.cc src/core/browser/client_win.cc)
        SET_EXECUTABLE_TARGET_PROPERTIES(hestia_desktop)
    else()
        add_executable(hestia_desktop
            ${DESKTOP_SRCS_COMMON} src/main_linux.cc src/core/browser/client_linux.cc)
        SET_EXECUTABLE_TARGET_PROPERTIES(hestia_desktop)
    endif()

    set_target_properties(hestia_desktop PROPERTIES OUTPUT_NAME "${APP_BINARY_NAME}")
    target_include_directories(hestia_desktop PRIVATE
        ${CEF_INCLUDE_PATH} src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(hestia_desktop PRIVATE
        hestia_shared hestia::frontend
        libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS}
        fmt::fmt spdlog::spdlog)

    if(UNIX AND NOT APPLE)
        # $ORIGIN finds the CEF libs beside the binary; $ORIGIN/.. lets the
        # AppImage build find libraries bundled one level up in usr/lib.
        set_target_properties(hestia_desktop PROPERTIES
            INSTALL_RPATH "$ORIGIN:$ORIGIN/.."
            BUILD_WITH_INSTALL_RPATH TRUE)
    endif()

    # Copy CEF runtime binaries and resources next to the executable.
    COPY_FILES(hestia_desktop "${CEF_BINARY_FILES}"   "${CEF_BINARY_DIR}"   "${CEF_TARGET_OUT_DIR}")
    COPY_FILES(hestia_desktop "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")

    # Windows sandbox bootstrap.
    if(WIN32 AND USE_SANDBOX)
        COPY_SINGLE_FILE(hestia_desktop "${CEF_BINARY_DIR}/bootstrap.exe"
                         "${CEF_TARGET_OUT_DIR}/${APP_BINARY_NAME}.exe")
        SET_LPAC_ACLS(hestia_desktop)
    endif()

    # Linux SUID helper.
    if(USE_SANDBOX AND UNIX AND NOT APPLE)
        SET_LINUX_SUID_PERMISSIONS(hestia_desktop
            "${CEF_TARGET_OUT_DIR}/chrome-sandbox")
    endif()
endif()

# ---- Strip CEF in Release (Linux) -------------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND UNIX AND NOT APPLE)
    add_custom_command(TARGET hestia_desktop POST_BUILD
        COMMAND "${CMAKE_STRIP}" --strip-debug
                "${CEF_TARGET_OUT_DIR}/libcef.so"
        COMMAND "${CMAKE_STRIP}" --strip-all
                "${CEF_TARGET_OUT_DIR}/libEGL.so"
        COMMAND "${CMAKE_STRIP}" --strip-all
                "${CEF_TARGET_OUT_DIR}/libGLESv2.so"
        COMMAND "${CMAKE_STRIP}" --strip-all
                "${CEF_TARGET_OUT_DIR}/libvk_swiftshader.so"
        COMMAND "${CMAKE_STRIP}" --strip-all
                "${CEF_TARGET_OUT_DIR}/libvulkan.so.1"
        COMMENT "Stripping CEF libraries" VERBATIM)

    add_custom_command(TARGET hestia_desktop POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DLOCALES_DIR=${CEF_TARGET_OUT_DIR}/locales
            "-DKEEP=en-US.pak"
            -P "${CMAKE_SOURCE_DIR}/cmake/PruneLocales.cmake"
        COMMENT "Pruning unused CEF locales" VERBATIM)
endif()

# ---- Strip our own binaries (Release, non-MSVC) -----------------------------
if(NOT MSVC)
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE    " -s")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_RELEASE " -s")
endif()

# ---- Install (desktop component, Linux/Windows) -----------------------------
# CEF requires its runtime (libcef, *.pak, locales, blobs, sandbox) to sit beside
# the executable, so the whole desktop runtime installs into one private dir.
if(NOT APPLE)
    # Plain "lib/hestia" (not CMAKE_INSTALL_LIBDIR) to dodge Debian multiarch
    # libdir drift — the postinst and .desktop both assume this fixed path.
    if(WIN32)
        # Flat at the install root (no FHS on Windows), matching the portable archive.
        set(HESTIA_DESKTOP_LIBDIR ".")
    else()
        set(HESTIA_DESKTOP_LIBDIR "lib/hestia")
    endif()

    install(TARGETS hestia_desktop
            RUNTIME DESTINATION "${HESTIA_DESKTOP_LIBDIR}" COMPONENT desktop
            LIBRARY DESTINATION "${HESTIA_DESKTOP_LIBDIR}" COMPONENT desktop)

    # Windows sandbox build: the app is a DLL; ship the renamed bootstrap launcher.
    if(WIN32 AND USE_SANDBOX)
        install(PROGRAMS "${CEF_TARGET_OUT_DIR}/${APP_BINARY_NAME}.exe"
                DESTINATION "${HESTIA_DESKTOP_LIBDIR}" COMPONENT desktop)
    endif()

    # CEF binaries (built copy: stripped in Release). chrome-sandbox must be
    # executable and SUID; install it executable and let deb/rpm postinst set SUID.
    foreach(_f IN LISTS CEF_BINARY_FILES)
        if(_f STREQUAL "chrome-sandbox")
            install(PROGRAMS "${CEF_TARGET_OUT_DIR}/${_f}"
                    DESTINATION "${HESTIA_DESKTOP_LIBDIR}" COMPONENT desktop)
        else()
            install(FILES "${CEF_TARGET_OUT_DIR}/${_f}"
                    DESTINATION "${HESTIA_DESKTOP_LIBDIR}" COMPONENT desktop)
        endif()
    endforeach()

    foreach(_f IN LISTS CEF_RESOURCE_FILES)
        if(IS_DIRECTORY "${CEF_RESOURCE_DIR}/${_f}")
            install(DIRECTORY "${CEF_TARGET_OUT_DIR}/${_f}/"
                    DESTINATION "${HESTIA_DESKTOP_LIBDIR}/${_f}" COMPONENT desktop)
        else()
            install(FILES "${CEF_TARGET_OUT_DIR}/${_f}"
                    DESTINATION "${HESTIA_DESKTOP_LIBDIR}" COMPONENT desktop)
        endif()
    endforeach()

    # Linux desktop-entry + icon so the launcher shows up in the application menu.
    if(UNIX)
        set(HESTIA_DESKTOP_EXEC "${CMAKE_INSTALL_PREFIX}/lib/hestia/HestiaLauncher")
        configure_file(
            "${CMAKE_SOURCE_DIR}/packaging/linux/hestia.desktop.in"
            "${CMAKE_CURRENT_BINARY_DIR}/hestia.desktop" @ONLY)
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hestia.desktop"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/applications" COMPONENT desktop)
        install(FILES "${CMAKE_SOURCE_DIR}/packaging/icons/hestia.svg"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
                COMPONENT desktop)
    endif()
endif()
