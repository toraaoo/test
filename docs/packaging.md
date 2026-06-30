# Packaging & release

How Hestia turns into installable artifacts. The packaging is driven by **CPack**
(configured in [`cmake/Packaging.cmake`](../cmake/Packaging.cmake)) plus a small
AppImage script; CI ([`.github/workflows/release.yml`](../.github/workflows/release.yml))
builds and publishes them on version tags.

## Artifacts

| Platform | Formats                                          |
|----------|--------------------------------------------------|
| Linux    | portable `.tar.gz`, `.deb`, `.rpm`, AppImage     |
| Windows  | portable `.zip`, NSIS `.exe`, WiX `.msi`         |

x86_64 only for now. Builds run on Linux and Windows runners.

## Components

The install tree is split into components:

- **`daemon`** — `hestiad`. The resident core; required, every front-end needs it.
- **`cli`** — `hestia` (CLI/TUI).
- **`desktop`** — the desktop launcher, the tray helper, and the bundled CEF
  runtime.
- **`Development`** — the static libs and headers. Never packaged; build-only.

How a component maps to a package depends on the format:

- **NSIS** presents a **component picker**: `daemon` + `cli` are preselected and
  required, `desktop` is opt-in. So a default install is CLI-only.
- **WiX MSI** presents the same picker as a WiX `FeatureTree` (auto-selected by
  CPack once components exist): `daemon` required and hidden, `cli` preselected,
  `desktop` opt-in — the MSI equivalent of the NSIS layout.
- **`.deb` / `.rpm`** are **monolithic** — one package with all runtime
  components. (`Development` is excluded.)
- **Portable archives** bundle everything in a **flat layout** at the archive
  root (see below), built by
  [`cmake/package_portable.cmake`](../cmake/package_portable.cmake) rather than
  CPack.

Only the command-line tools go in `bin/`: the `daemon` (`hestiad`) and the `cli`
(`hestia`). When `cli` is kept selected in the picker the NSIS installer puts that
`bin/` on `PATH`, so both are runnable from anywhere; deselect `cli` and the
`PATH` entry is skipped. It's written with the EnVar plugin because the built-in
NSIS path macro overflows when the system `PATH` is long. The GUI binaries (the
launcher and the tray) install **outside** `bin/`, so they never land on `PATH`.

## The desktop layout

CEF requires its runtime (`libcef`, `*.pak`, `locales`, blobs, sandbox) to sit
beside the executable, so the desktop installs as a self-contained unit:

- Installed packages (`.deb`/`.rpm`/installers): Linux puts the launcher, the
  tray, and the CEF runtime in `lib/hestia/` (with a `.desktop` entry + icon in
  `share/`); Windows installs them **flat at the install root** (Windows has no
  FHS to honour). The NSIS installer creates Start-menu and Desktop shortcuts
  only when the `desktop` component is selected, so a CLI-only install leaves no
  dangling launcher link.
- Portable archives: the same layout as the Windows install — the daemon and CLI
  in `bin/`, and the tray, launcher, and CEF runtime at the archive root, so the
  app is the obvious thing to double-click and nothing is buried in `lib/`.

The on-disk binary is `HestiaLauncher` (not `Hestia`) so it doesn't collide with
the `hestia` CLI on case-insensitive Windows. The window/app identity is still
`Hestia` (`APP_NAME`/`APP_ID`); only the filename differs.

### The CEF sandbox

The sandbox helper must be SUID root. `.deb`/`.rpm` set this in a `postinst`
([`packaging/linux/postinst`](../packaging/linux/postinst)). An AppImage can't carry
a SUID binary, so the AppImage launcher runs with the sandbox disabled
(`--no-sandbox`). On Windows the sandbox uses the CEF bootstrap launcher.

## Windows installers: NSIS `.exe` vs MSI

Windows ships **two** installers, by design, mirroring how Tauri and Electron
package for Windows:

- **NSIS `.exe`** — the interactive end-user installer. It owns anything that
  needs a *runtime* choice (and is where a per-user/per-machine selection would
  live if we add one). It installs per-machine today.
- **WiX `.msi`** — a **per-machine** package for managed/enterprise deployment
  (SCCM, Intune, Group Policy), which expect an MSI, silent `msiexec /i`, and a
  stable upgrade code. It installs to `Program Files`, puts the CLI's `bin\` on
  the **system** `PATH`, and requires elevation.

### Why the MSI is per-machine only

We deliberately do **not** try to make the MSI offer a per-user/per-machine
choice at install time. CPack's WiX generator can only bake a single
`CPACK_WIX_INSTALL_SCOPE` (`perMachine`/`perUser`) into the package — there is no
dual-purpose mode — and Windows Installer itself fights it (`perMachine` forces
`ALLUSERS=1`; `perUser` sets a no-elevation bit). A real in-installer scope
picker needs a hand-authored WiX template (`WixUI_Advanced` wired to
`APPLICATIONFOLDER`), which bypasses most of CPack and is fragile.

The wider ecosystem draws the same line: Tauri's and Electron's runtime
scope-selection (`installMode: both`) is an **NSIS** feature; their MSI is the
per-machine, enterprise artifact. Hestia follows that split — the MSI is for
managed deployment, and the user-facing choice belongs to the NSIS `.exe`.

### MSI internals

Driven by CPack (`cmake/Packaging.cmake`, the `CPACK_WIX_*` block), targeting
**WiX Toolset v3** (`candle`/`light`):

- **Upgrade code** — `CPACK_WIX_UPGRADE_GUID` is a fixed GUID so each release's
  MSI upgrades the previous one in place (the template's `MajorUpgrade`). It must
  never change.
- **`PATH`** — appended via a `CPACK_WIX_PATCH_FILE`
  ([`packaging/windows/wix/path_env.xml`](../packaging/windows/wix/path_env.xml))
  that injects an `<Environment>` element into the `cli` component, so the entry
  is added/removed exactly with that feature. Unlike the NSIS path (which had to
  abandon the built-in `CPACK_NSIS_MODIFY_PATH` macro for the EnVar plugin
  because that macro overflows NSIS's fixed string buffer on a long system
  `PATH`), the MSI uses Windows Installer's native `WriteEnvironmentStrings`
  action — it rewrites the `PATH` value directly with no length cap, so a long
  pre-existing `PATH` is handled correctly.
- **Shortcuts** — Start-menu and Desktop shortcuts for the launcher come from
  `CPACK_PACKAGE_EXECUTABLES` / `CPACK_CREATE_DESKTOP_LINKS`. The NSIS generator
  clears them in `cmake/CPackOptions.cmake` (it rolls its own conditional
  shortcuts), so they apply to the MSI only. The shortcut lives in the `desktop`
  component, so a CLI-only install leaves none.
- **License** — WiX needs an RTF; the extensionless repo `LICENSE` can't be
  converted, so the MSI uses
  [`packaging/windows/license.rtf`](../packaging/windows/license.rtf).

## Building locally

```bash
# Everything, platform-default formats (+ AppImage if the tools are present):
scripts/package.sh

# A single generator:
scripts/package.sh TGZ
```

Packages land in `build/`. The AppImage needs `linuxdeploy` and `appimagetool`
on `PATH`.

On Windows, a configured build produces both installers via `cpack -G NSIS` and
`cpack -G WIX` (or plain `cpack`, which runs both). The MSI needs **WiX Toolset
v3** (`candle`/`light`) on `PATH` — e.g. `choco install wixtoolset`.

## CI caching

Release and CI builds reuse:

- **ccache / sccache** (compiler cache) — `hendrikmuhs/ccache-action`
- **CEF** — `third_party/cef`, keyed on the CEF version
- **Bun deps** — `apps/desktop/frontend/node_modules`, keyed on `bun.lock`

First run is cold (CEF is a ~1 GB download); subsequent runs are warm.
