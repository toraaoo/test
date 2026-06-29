# Hestia

A Minecraft launcher built in modern C++.

Alongside a beautiful desktop UI, Hestia ships first-class **CLI** and **TUI**
front-ends, so it's just as comfortable from a terminal as from a window.

> **Status:** early development (`v0.0.1`). The project is being scaffolded —
> the build system, logging, a config store, and the CLI/TUI skeleton are in
> place. Launcher functionality is not implemented yet. Expect things to change.

## Front-ends

Hestia is one daemon-backed core with several ways to drive it:

- **Desktop** (`Hestia`) — the graphical launcher. The primary, "beautiful UI"
  experience.
- **CLI** (`hestia`) — scriptable command-line interface for automation and
  power users.
- **TUI** (`hestia tui`) — a full interactive terminal interface for working
  over SSH or without a desktop session.
- **Tray** (`hestia-tray`) — a resident system-tray helper showing daemon status
  and a one-click toggle for starting the daemon at login.

The CLI and TUI live in the **same binary**: running `hestia` with no subcommand
shows usage, and `hestia tui` launches the interactive interface.

## Project layout

```
hestia-cpp/
├── libs/shared/             hestia_shared — IPC transport + protocol, client SDK, identity, logging
├── libs/engine/             hestia_engine — launcher engine (config, greeting, …); daemon-internal
├── libs/tui/                hestia_tui  — terminal UI library (FTXUI)
├── apps/desktop/            Hestia      — graphical desktop launcher (CEF + React)
│   ├── frontend/            Vite + React + TypeScript UI (built with Bun)
│   └── src/core/            CEF shell — process model, IPC bridge, window, scheme
├── apps/cli/                hestia      — CLI + the `tui` subcommand (CLI11)
├── apps/daemon/             hestiad     — the daemon: IPC, supervision, autostart
├── apps/tray/               hestia-tray — resident system-tray helper (GDBus SNI)
├── cmake/                   CMake helpers (DownloadCEF, CMakeRC, PruneLocales)
└── third_party/             vendored C++ dependencies (git submodules)
```

The dependency arrows are one-way and enforced by the build. Every front-end and
the daemon link `hestia_shared` (transport + client SDK). `hestia_engine` (the
launcher logic) is daemon-only — front-ends reach it over the socket rather than
by linking (the desktop app still links it directly today, a transitional
exception being retired). `tui` and `cli` both depend on `shared`; `cli` links
`tui`; nothing depends back on `cli`. The TUI exposes exactly one public symbol,
`hestia::tui::run()`. See [docs/architecture.md](docs/architecture.md) for the
full picture.

## Tech stack

- **C++20**, **CMake** (≥ 3.21), built with Ninja
- [spdlog](https://github.com/gabime/spdlog) + [fmt](https://github.com/fmtlib/fmt) — logging and formatting
- [CLI11](https://github.com/CLIUtils/CLI11) — command-line parsing
- [FTXUI](https://github.com/ArthurSonzogni/FTXUI) — terminal user interface
- [CEF](https://bitbucket.org/chromiumembedded/cef) — Chromium Embedded Framework (desktop only)
- [React](https://react.dev/) + [Vite](https://vitejs.dev/) + [Bun](https://bun.sh/) — desktop frontend

C++ dependencies are vendored as git submodules under `third_party/`. CEF is
fetched automatically at configure time (~1 GB, gitignored).

## Building

A single configure builds everything — the CLI/TUI **and** the desktop launcher.

Clone with submodules:

```bash
git clone --recurse-submodules <repo-url>
cd hestia-cpp
# already cloned? fetch the submodules:
git submodule update --init --recursive
```

**Build ordering matters**: the desktop frontend must be compiled before CMake
configures (CMakeRC embeds the `dist/` tree at configure time). So build the
frontend first, then configure and build:

```bash
# 1. Build the frontend (produces apps/desktop/frontend/dist)
(cd apps/desktop/frontend && bun install && bun run build)

# 2. Configure + build (first run downloads ~1 GB of CEF into third_party/cef)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Binaries land in `build/Release/` (or `build/<config>/`):

- `hestia` — CLI / TUI
- `HestiaLauncher` — desktop launcher (named `HestiaLauncher` so it doesn't
  collide with the `hestia` CLI on case-insensitive filesystems; the app still
  presents itself as "Hestia")

**Release** is sandboxed, stripped, and serves the embedded frontend. On Linux,
make the sandbox helper SUID root once per build location:

```bash
sudo chown root:root build/Release/chrome-sandbox
sudo chmod 4755     build/Release/chrome-sandbox
./build/Release/HestiaLauncher
```

**Dev mode** (hot-reload against the Vite dev server, sandbox off):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
# In a second terminal:
(cd apps/desktop/frontend && bun run dev)
# Then run with the dev-server URL:
./build/Debug/HestiaLauncher --dev-url=http://localhost:5173
```

> A `dist/` must exist at configure time even in Debug (CMakeRC needs it). Run
> one `bun run build` first; the dev server overrides it at runtime anyway.

### Build options

| Option               | Default | Description                                            |
|----------------------|---------|--------------------------------------------------------|
| `BUILD_DESKTOP`      | `ON`    | Build the desktop launcher — pulls in CEF              |
| `BUILD_CLI`          | `ON`    | Build the `hestia` CLI / TUI front-end                 |
| `BUILD_TUI`          | `ON`    | Build the TUI library (forced on when `BUILD_CLI` is)  |
| `USE_CCACHE`         | `ON`    | Route compiles through `ccache` when it is installed   |
| `USE_SANDBOX`        | auto    | CEF sandbox — `ON` in Release, `OFF` in Debug          |
| `CEF_VERSION`        | pinned  | CEF distribution version string (set in desktop CMake) |
| `APP_DEV_SERVER_URL` | `""`    | Configure-time dev-server URL (Debug only)             |

The daemon (`hestiad`) and tray (`hestia-tray`) are the resident core and are
always built; the front-ends above are opt-out.

### Faster dev builds

The desktop launcher is by far the heaviest target: it triggers the ~1 GB CEF
download at configure time and a long link. When you're iterating on the
daemon, CLI, or TUI, configure a separate build that skips it entirely — this
also skips CEF's download and every dependency only the desktop needs (vendored
deps build on demand):

```bash
# A dev configure that never touches CEF
cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_DESKTOP=OFF

# Tightest loop: build only the target you're working on
cmake --build build-dev --target hestia_daemon   # or hestia_cli
```

Installing `ccache` makes rebuilds across configures reuse cached object files;
it is picked up automatically (disable with `-DUSE_CCACHE=OFF`).

The [`scripts/`](scripts/) helpers wrap all of this so you don't have to remember
the flags — `scripts/build.sh daemon`, `scripts/run.sh cli`, `scripts/run.sh
desktop` (with frontend hot-reload), `scripts/build.sh --release`. They double as
CI steps. See [scripts/README.md](scripts/README.md).

## Usage

```bash
# Show help
hestia

# Launch the interactive terminal UI
hestia tui

# A friendly greeting (demo command)
hestia greet --name Ada

# Configuration (flat key=value store)
hestia config set <key> <value>
hestia config get <key>
hestia config home              # print the resolved data directory
hestia config set-home <dir>    # persist the data dir for future runs

# Autostart (start the background daemon at login)
hestia autostart enable          # register the daemon to start at login
hestia autostart disable         # remove the registration
hestia autostart status          # print "enabled" or "disabled"

# Logging verbosity (global flags, accepted at any position)
hestia -v greet   # verbose / debug logging
hestia -q greet   # warnings and errors only

# Override the data directory for one run
hestia --home /path/to/dir config home

# Version
hestia --version
```

The data directory is resolved as: `--home` → `$HESTIA_HOME` → a persisted
pointer (`config set-home`) → the platform default (`~/.hestia`, or
`%APPDATA%\Hestia` on Windows).

## Documentation

- **[docs/architecture.md](docs/architecture.md)** — the as-built map: target
  graph, core/TUI/CLI boundaries, and the TUI's component model.
- **[docs/contributing.md](docs/contributing.md)** — conventions and step-by-step
  recipes for adding a view, layout, component, overlay, or CLI command.
- **[docs/packaging.md](docs/packaging.md)** — how releases are built: formats,
  the component model, the desktop/CEF layout, and CI caching.

## License

[MIT](LICENSE) © 2026 toraaoo
