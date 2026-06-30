# Packaging.cmake — CPack config for the distro packages and Windows installer.
# Portable archives are built separately (cmake/package_portable.cmake); AppImage
# via packaging/appimage/build-appimage.sh.
#
#   Linux   : DEB, RPM
#   Windows : NSIS (.exe), WiX (.msi)
#
# Component model:
#   daemon      required           — hestiad
#   cli                            — hestia (CLI/TUI)
#   desktop     optional           — desktop launcher + tray + CEF runtime
#   Development  (libs/headers)     — never packaged
#
# DEB/RPM are monolithic (all runtime components in one); NSIS presents the
# component picker so "CLI only" is the default and desktop is opt-in.

include(GNUInstallDirs)

set(CPACK_PACKAGE_NAME    "hestia")
set(CPACK_PACKAGE_VENDOR  "${APP_VENDOR}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "${APP_VENDOR}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fast, lightweight Minecraft launcher (CLI/TUI + desktop)")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Hestia")
set(CPACK_VERBATIM_VARIABLES TRUE)
set(CPACK_STRIP_FILES TRUE)

# Grouping/component-install differs per generator (see the file).
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_SOURCE_DIR}/cmake/CPackOptions.cmake")

# Runtime components. The daemon is the resident core every front-end needs; the
# CLI and desktop are separately selectable on top of it.
set(_hestia_components daemon cli)
if(BUILD_DESKTOP)
    list(APPEND _hestia_components desktop)
endif()
set(CPACK_COMPONENTS_ALL ${_hestia_components})

set(CPACK_COMPONENT_DAEMON_DISPLAY_NAME "Daemon")
set(CPACK_COMPONENT_DAEMON_DESCRIPTION  "The hestiad background daemon.")
set(CPACK_COMPONENT_DAEMON_REQUIRED TRUE)
set(CPACK_COMPONENT_DAEMON_HIDDEN TRUE)

set(CPACK_COMPONENT_CLI_DISPLAY_NAME "Command-line tools")
set(CPACK_COMPONENT_CLI_DESCRIPTION  "The hestia CLI and TUI.")
set(CPACK_COMPONENT_CLI_DEPENDS daemon)

set(CPACK_COMPONENT_DESKTOP_DISPLAY_NAME "Desktop launcher")
set(CPACK_COMPONENT_DESKTOP_DESCRIPTION  "Graphical desktop launcher and system-tray helper.")
set(CPACK_COMPONENT_DESKTOP_DEPENDS daemon)
set(CPACK_COMPONENT_DESKTOP_DISABLED TRUE)

# Portable archive naming: hestia-<version>-<os>-x86_64.{tar.gz,zip}
string(TOLOWER "${CMAKE_SYSTEM_NAME}" _os)
set(CPACK_ARCHIVE_FILE_NAME "hestia-${PROJECT_VERSION}-${_os}-x86_64")

# ---- Linux: DEB / RPM -------------------------------------------------------
if(UNIX AND NOT APPLE)
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${APP_VENDOR}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "games")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_SOURCE_DIR}/packaging/linux/postinst")

    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
    set(CPACK_RPM_PACKAGE_AUTOREQ ON)
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/packaging/linux/postinst")
    # Don't claim ownership of system dirs the distro already provides.
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/lib /usr/bin /usr/share /usr/share/applications /usr/share/icons
        /usr/share/icons/hicolor /usr/share/icons/hicolor/scalable
        /usr/share/icons/hicolor/scalable/apps)
endif()

# ---- Windows: NSIS ----------------------------------------------------------
if(WIN32)
    set(CPACK_NSIS_PACKAGE_NAME "Hestia")
    set(CPACK_NSIS_DISPLAY_NAME "Hestia")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MANIFEST_DPI_AWARE TRUE)
    set(CPACK_NSIS_MUI_ICON     "${CMAKE_SOURCE_DIR}/packaging/icons/hestia.ico")
    set(CPACK_NSIS_MUI_UNIICON  "${CMAKE_SOURCE_DIR}/packaging/icons/hestia.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\hestia.exe")

    # Steps gated on the picker via each component's section flag (${cli}/${desktop});
    # via EXTRA_*_COMMANDS rather than CPACK_NSIS_MENU_LINKS, which emits shortcuts
    # unconditionally and would dangle on a CLI-only install. PATH uses EnVar — the
    # built-in CPACK_NSIS_MODIFY_PATH macro overflows NSIS's 1024-char limit on a
    # long PATH; the plugin DLL is installed by CI.
    set(_nsis_install
        "SectionGetFlags \${cli} $0\n  IntOp $0 $0 & \${SF_SELECTED}\n  IntCmp $0 0 hestia_skip_path\n    EnVar::SetHKLM\n    EnVar::AddValueEx 'Path' '$INSTDIR\\\\bin'\n    Pop $0\n  hestia_skip_path:")
    set(_nsis_uninstall
        "EnVar::SetHKLM\n  EnVar::DeleteValue 'Path' '$INSTDIR\\\\bin'\n  Pop $0")

    if(BUILD_DESKTOP)
        string(APPEND _nsis_install
            "\n  SectionGetFlags \${desktop} $0\n  IntOp $0 $0 & \${SF_SELECTED}\n  IntCmp $0 0 hestia_skip_icons\n    CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Hestia.lnk' '$INSTDIR\\\\${APP_BINARY_NAME}.exe'\n    CreateShortCut '$DESKTOP\\\\Hestia.lnk' '$INSTDIR\\\\${APP_BINARY_NAME}.exe'\n  hestia_skip_icons:")
        string(APPEND _nsis_uninstall
            "\n  Delete '$SMPROGRAMS\\\\$START_MENU\\\\Hestia.lnk'\n  Delete '$DESKTOP\\\\Hestia.lnk'")
    endif()

    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "${_nsis_install}")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "${_nsis_uninstall}")
endif()

# ---- Windows: WiX MSI -------------------------------------------------------
# A per-machine MSI for managed/enterprise deployment (Program Files, system
# PATH, admin) — the format SCCM/Intune/GPO expect. Runtime per-user/per-machine
# selection is deliberately NOT offered here: CPack's WiX generator can't emit a
# dual-purpose MSI, and the wider ecosystem (Tauri, Electron) keeps that choice
# in the NSIS .exe — which Hestia also ships. See docs/packaging.md.
if(WIN32)
    set(CPACK_WIX_VERSION 3)
    set(CPACK_WIX_INSTALL_SCOPE perMachine)
    # Stable across releases so each MSI upgrades the previous in place (the
    # template's MajorUpgrade keys on it). Generated once; never change it.
    set(CPACK_WIX_UPGRADE_GUID "7C9A2F4E-3B1D-4A6E-9F2C-8D5B1E0A4C73")
    set(CPACK_WIX_PRODUCT_ICON "${CMAKE_SOURCE_DIR}/packaging/icons/hestia.ico")
    set(CPACK_WIX_PROGRAM_MENU_FOLDER "Hestia")
    # CPACK_RESOURCE_FILE_LICENSE (the repo LICENSE) has no extension, which the
    # WiX generator can't convert; point it at a ready-made RTF instead.
    set(CPACK_WIX_LICENSE_RTF "${CMAKE_SOURCE_DIR}/packaging/windows/license.rtf")
    # Append the CLI's bin/ to the system PATH, gated on the cli component. The
    # component picker itself is the default WixUI_FeatureTree (auto-selected
    # once CPack components exist), mirroring the NSIS picker.
    set(CPACK_WIX_PATCH_FILE "${CMAKE_SOURCE_DIR}/packaging/windows/wix/path_env.xml")
    # Start-menu + Desktop shortcuts for the launcher. The shortcut lives in the
    # desktop component, so a CLI-only install gets none. The NSIS installer
    # rolls its own conditional shortcuts and clears these in CPackOptions.
    set(CPACK_PACKAGE_EXECUTABLES "${APP_BINARY_NAME};Hestia")
    set(CPACK_CREATE_DESKTOP_LINKS "${APP_BINARY_NAME}")
endif()

# Portable archives are built by cmake/package_portable.cmake; CPack drives the
# installers and distro packages. Windows ships both the NSIS .exe and the MSI.
if(WIN32)
    set(CPACK_GENERATOR "NSIS;WIX")
elseif(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB;RPM")
endif()

include(CPack)
