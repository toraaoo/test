# Included once per generator with CPACK_GENERATOR set, so grouping differs
# between distro packages and installers.
if(CPACK_GENERATOR MATCHES "^(DEB|RPM)$")
    # One runtime-only package (daemon + cli + desktop), Development excluded.
    set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
    set(CPACK_DEB_COMPONENT_INSTALL ON)
    set(CPACK_RPM_COMPONENT_INSTALL ON)
elseif(CPACK_GENERATOR STREQUAL "NSIS")
    set(CPACK_COMPONENTS_GROUPING IGNORE)
    # NSIS rolls its own component-gated shortcuts (EXTRA_INSTALL_COMMANDS);
    # drop the auto-shortcut vars the MSI uses so they don't dangle here.
    unset(CPACK_PACKAGE_EXECUTABLES)
    unset(CPACK_CREATE_DESKTOP_LINKS)
elseif(CPACK_GENERATOR STREQUAL "WIX")
    # Per-component features so the MSI shows the daemon/cli/desktop picker.
    set(CPACK_COMPONENTS_GROUPING IGNORE)
endif()
