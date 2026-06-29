# Daemon protocol & architecture

This is the contract for Hestia's daemon (`hestiad`) and the frontends that
drive it. It is the **Phase 0** artifact of the daemon migration: it freezes the
boundary so the rest of the work doesn't churn it.

> **Status:** the transport, endpoint resolution, single-instance guard, JSON
> envelope, request router, and the typed client SDK exist; `config`, `app.info`,
> and `app.greet` are served by the daemon, and the CLI drives them over the bridge
> (auto-spawning the daemon on first use). Process supervision (`process.*`) is
> implemented: processes are spawned detached, logged to disk, and re-adopted on
> daemon restart. The connection is **multiplexed** — many requests in flight at
> once, correlated by `id` — and the daemon pushes a **live event stream** (log
> lines, state changes) to subscribers. **Auto-restart** of crashed processes is
> enforced by a background supervision loop. **Autostart** (start the daemon at
> login) is implemented for every platform, and a resident **tray helper**
> (`hestia-tray`) surfaces daemon status and toggles autostart from the system tray.

## Why a daemon

Hestia supervises processes that must **outlive the UI that started them** —
game servers and game client instances that keep running after the desktop
window, CLI, or TUI exits. A library linked into each frontend cannot own those
processes; a long-lived daemon can. This mirrors the established pattern for
"always-on backend + multiple frontends": Docker (`dockerd` + `docker`),
Tailscale (`tailscaled` + CLI/GUI), Syncthing, and Pterodactyl (`wings`) all put
all state and process lifecycle in one daemon and make every frontend a thin
client over a local socket.

## One backend, thin clients

```
hestiad ── links ──► hestia_engine      (the engine; daemon-internal only)
   ▲
   │ local socket (this protocol)
   ├── hestia (CLI / TUI)
   ├── Hestia (CEF desktop)
   └── tray helper
```

`hestia_engine` (formerly `hestia_core`) is the daemon's **internal** library —
the equivalent of Tailscale's `LocalBackend`. Frontends do **not** link it; they
link `hestia_shared`, which carries the transport, the protocol envelope, and the
typed client SDK (`hestia::client::Client`). There is exactly one public boundary:
the socket. Two libraries total — `shared` (common to daemon + clients) and
`engine` (daemon-only); the client SDK is part of `shared`, not a separate lib.

## Endpoint

A single-machine, per-user endpoint. The transport is platform-specific but the
path/name resolution is centralized in `hestia::ipc`:

| Platform     | Endpoint                                              |
|--------------|------------------------------------------------------|
| Linux        | Unix socket at `$XDG_RUNTIME_DIR/hestia/hestiad.sock` (falls back to `/tmp/hestia-<uid>/hestiad.sock`) |
| macOS        | Unix socket under the user's run dir *(planned)*     |
| Windows      | Named pipe `\\.\pipe\hestia-<user>` *(planned)*      |

The runtime socket dir is deliberately **not** the data dir (`hestia_core`'s
`data_home`): the data dir holds persistent state, the runtime dir holds the
ephemeral socket. They have different lifetimes and permissions.

## Framing

Every message — request or response — is a single length-prefixed frame:

```
[ uint32 length, network byte order ][ length bytes of payload ]
```

The transport treats the payload as **opaque bytes**. Framing is the transport's
only job; message semantics live one layer up. Max frame size is capped (16 MiB)
to fail fast on a desync rather than allocate unboundedly.

### Connection model

The connection is a **full-duplex, multiplexed** frame pipe. A client holds one
persistent connection and may have many requests in flight at once; each carries
an `id` and the daemon echoes it on the matching response, so a background reader
demultiplexes replies to the right caller. The daemon also pushes **events**
(unsolicited frames, no `id`) down the same connection to subscribers. Server-side,
each accepted connection is served on its own thread; events are fanned out from
the supervision loop.

## Messages

### Phase 1 (now): bare-channel frames

To avoid a JSON dependency before the message set justifies one, Phase 1 frames
carry the **channel name as a bare string**:

- Request payload: the channel, e.g. `health.ping`.
- Response payload: a small JSON string built by hand, e.g.
  `{"ok":true,"status":"alive","pid":12345}`.

| Channel       | Request | Response                                  |
|---------------|---------|-------------------------------------------|
| `health.ping` | *(none)*| `{"ok":true,"status":"alive","pid":<n>}`  |

### Phase 2 (now): JSON envelope

Frames are a JSON object; `nlohmann/json` backs the codec. Channels served today:

| Channel          | Request payload          | Response payload                         |
|------------------|--------------------------|------------------------------------------|
| `health.ping`    | *(none)*                 | `{status, pid}`                          |
| `app.info`       | *(none)*                 | `{name, version, id, vendor, channel}`   |
| `app.greet`      | `{name?}`                | `{message}`                              |
| `config.get`     | `{key}`                  | `{value}` or error `not_found`           |
| `config.set`     | `{key, value}`           | *(empty)*                               |
| `config.home`    | *(none)*                 | `{path}`                                 |
| `config.set-home`| `{dir?}`                 | `{path}` (re-resolved immediately)       |

The envelope reuses the desktop CEF bridge's shape for consistency:

```jsonc
// request
{ "id": 7, "channel": "config.get", "payload": { "key": "java.path" } }
// response
{ "id": 7, "ok": true, "payload": { "value": "/usr/bin/java" } }
{ "id": 7, "ok": false, "error": { "code": "not_found", "message": "..." } }
```

`id` correlates each response to its request over the multiplexed connection. The
client assigns it; the daemon echoes it back. It is optional only for one-shot
tools (e.g. `hestiad ping`) that issue a single request.

### Process control (now)

The daemon owns every launched process so it outlives the frontend. Processes are
spawned via a double-fork (reparented to init, so they survive a daemon crash),
with stdout/stderr redirected to a per-process log file at the OS level.

| Channel          | Request payload                              | Response payload                |
|------------------|----------------------------------------------|---------------------------------|
| `process.start`  | `{id, kind, program, args?, cwd?, restart?}` | the process record              |
| `process.stop`   | `{id}`                                        | *(empty)*                      |
| `process.list`   | *(none)*                                      | `{processes: [record, …]}`      |
| `process.status` | `{id}`                                        | the record, or error `not_found`|
| `process.logs`   | `{id, lines?}`                                | `{text}` (last N log lines)     |

The `restart` policy is `{auto: bool, max_retries: int, backoff_ms: int}`, where
`max_retries: 0` means restart without limit.

A process **record** is `{id, kind: server|instance, pid, start_time, log_path,
state, program, args, cwd, restart, restarts}`. The launch fields (`program`,
`args`, `cwd`, `restart`) are persisted so a restarted daemon can relaunch, not
just observe. `state` is one of:

| State      | Meaning                                                         |
|------------|-----------------------------------------------------------------|
| `starting` | spawned, not yet confirmed running                              |
| `running`  | alive (pid live and start time matches)                         |
| `crashed`  | exited **without** an operator stop — eligible for auto-restart |
| `exited`   | stopped via `process.stop` — terminal, never auto-restarted     |

The table is persisted to `<data_home>/processes.json`; on startup `reconcile()`
re-adopts any process whose pid is still alive **and** whose start time matches
(guarding against PID reuse), and marks the rest `crashed`.

### Events & subscription

A client opts into the event stream with `events.subscribe`; thereafter the daemon
pushes event frames down that connection. An optional `id` scopes the stream to a
single process; omit it for all.

| Channel            | Request payload | Response payload     |
|--------------------|-----------------|----------------------|
| `events.subscribe` | `{id?}`         | `{subscribed: true}` |

An **event frame** has no `id` and is shaped `{ "event": <topic>, "payload": … }`:

| Topic           | Payload                          | When                                  |
|-----------------|----------------------------------|---------------------------------------|
| `process.state` | the process record               | a process changes state               |
| `process.log`   | `{id, text}` (a chunk of output) | new bytes are appended to its log file |

```jsonc
{ "event": "process.state", "payload": { "id": "srv", "state": "crashed", … } }
{ "event": "process.log",   "payload": { "id": "srv", "text": "Done (4.2s)!\n" } }
```

Events are produced by the supervision loop, so a state change is visible within
one poll interval. The history before a subscription is available via
`process.logs`; the stream carries output from the subscription point onward.

### Auto-restart

The supervision loop polls liveness, streams new log output, and enforces each
process's `restart` policy: a `crashed` process with `auto: true` is relaunched
after `backoff_ms`, up to `max_retries` (unlimited when `0`). An operator
`process.stop` is terminal (`exited`) and is never restarted. Restarts reuse the
same log file, so the stream is continuous across a restart.

### Autostart

Register the daemon to start with the user session. The daemon writes the
registration against *its own* executable path (resolved from the OS), so it keeps
pointing at the binary even if it moves.

| Channel             | Request payload | Response payload      |
|---------------------|-----------------|-----------------------|
| `autostart.enable`  | *(none)*        | `{enabled: true}`     |
| `autostart.disable` | *(none)*        | `{enabled: false}`    |
| `autostart.status`  | *(none)*        | `{enabled: <bool>}`   |

Each platform uses its native mechanism, behind the `Autostart` seam:

| Platform | Mechanism                                                              |
|----------|-----------------------------------------------------------------------|
| Linux    | systemd **user unit** (`~/.config/systemd/user/hestiad.service` + a `default.target.wants` symlink, managed natively so it works without a running user manager) |
| macOS    | **LaunchAgent** plist (`~/Library/LaunchAgents/<app-id>.plist`, `RunAtLoad`) |
| Windows  | logon **Scheduled Task** (`schtasks /SC ONLOGON`)                     |

## Auth

The socket is per-user and lives in a user-private runtime dir. Phase 1 relies on
filesystem permissions. Phase 8 adds a token (handed to clients via the data dir)
and tightens socket mode, matching Syncthing's API-key model.

## Versioning

Every envelope carries a protocol major version in its `v` field (currently `1`).
On connect, the client performs a one-shot handshake and refuses an incompatible
major with a clear error rather than guessing — so a client/daemon skew fails fast
instead of mis-parsing later messages. Additive fields within a major stay
compatible and do not bump the version. The version constant and the compatibility
check live in `hestia/ipc/protocol.h`.

## Cross-platform seams

All platform divergence is confined to a few interfaces. Everything above them —
protocol, process table, log format — is platform-neutral.

| Seam                | Header                                  | Linux            | macOS            | Windows                    |
|---------------------|-----------------------------------------|------------------|------------------|----------------------------|
| `IpcTransport`      | `hestia/ipc/transport.h`                | Unix socket ✅    | Unix socket 🔜    | Named pipe 🔜               |
| `ProcessSupervisor` | `apps/daemon/src/process_supervisor.h`  | ✅ (`pidfd`/subreaper 🔜) | `kqueue` 🔜   | Job Objects 🔜              |
| `Autostart`         | `apps/daemon/src/autostart.h`           | systemd user ✅   | LaunchAgent ✅¹   | logon Scheduled Task ✅¹    |
| `TrayBackend`       | `apps/tray/src/tray_backend.h`          | SNI over GDBus ✅ | NSStatusItem ✅¹  | Shell_NotifyIcon ✅¹        |

✅ implemented · 🔜 planned · ¹ written, verified on Linux only (no macOS/Windows
toolchain in CI yet)

The tray helper (`hestia-tray`) is a resident thin client: it links the client
SDK and a `TrayBackend`, never the engine. On Linux the backend speaks the
`org.kde.StatusNotifierItem` + `com.canonical.dbusmenu` D-Bus protocols directly
over GDBus, so its only dependency is glib/gio — no GUI toolkit — which keeps the
CLI headless-safe and the desktop app the only heavyweight frontend. The dbusmenu
side is deliberately host-agnostic: it answers both the per-item `Event` and the
batched `EventGroup` click paths (and `AboutToShow`/`AboutToShowGroup`), and it
re-registers with the `StatusNotifierWatcher` every time it appears, so the icon
survives a panel reload or a login race.

## Open decision (gates Phases 5–6)

**Windows: logon Scheduled Task vs. Windows Service.** A Service survives logout
but runs in session 0 and must use `CreateProcessAsUser` to launch GUI client
instances on the user's desktop. A logon task runs in the user session (GUI
instances "just work") but dies at logout. Default: **logon task** until a
"survive logout on Windows" requirement actually appears.
