# Daemon protocol & architecture

This is the contract for Hestia's daemon (`hestiad`) and the frontends that
drive it. It is the **Phase 0** artifact of the daemon migration: it freezes the
boundary so the rest of the work doesn't churn it.

> **Status:** the transport, endpoint resolution, single-instance guard, JSON
> envelope, request router, and the typed client SDK exist; `config`, `app.info`,
> and `app.greet` are served by the daemon, and the CLI drives them over the bridge
> (auto-spawning the daemon on first use). Process supervision (`process.*`) is
> implemented: processes are spawned detached, logged to disk, and re-adopted on
> daemon restart. A live event/log stream and autostart are **planned**.

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
hestiad ── links ──► hestia_core        (the engine; daemon-internal only)
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

`id` correlates responses once the connection is multiplexed (Phase 3 event
stream). Until then it is optional.

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

A process **record** is `{id, kind: server|instance, pid, start_time, log_path,
state: starting|running|exited|crashed}`. The table is persisted to
`<data_home>/processes.json`; on startup `reconcile()` re-adopts any process whose
pid is still alive **and** whose start time matches (guarding against PID reuse).

### Later: events + auto-restart

A live event stream (long-poll or SSE-style frames) for log lines and status
changes, and periodic auto-restart enforcement of `restart_policy`, require the
multiplexed connection and are still planned.

## Auth

The socket is per-user and lives in a user-private runtime dir. Phase 1 relies on
filesystem permissions. Phase 8 adds a token (handed to clients via the data dir)
and tightens socket mode, matching Syncthing's API-key model.

## Versioning

The envelope carries a protocol version once it exists (Phase 2). Client/daemon
skew is handled by refusing incompatible majors with a clear error rather than
guessing.

## Cross-platform seams

All platform divergence is confined to three interfaces. Everything above them —
protocol, process table, log format — is platform-neutral.

| Seam                | Header                                  | Linux            | macOS            | Windows                    |
|---------------------|-----------------------------------------|------------------|------------------|----------------------------|
| `IpcTransport`      | `hestia/ipc/transport.h`                | Unix socket ✅    | Unix socket 🔜    | Named pipe 🔜               |
| `ProcessSupervisor` | `apps/daemon/src/process_supervisor.h`  | `pidfd`, subreaper 🔜 | `kqueue` 🔜   | Job Objects 🔜              |
| `Autostart`         | `apps/daemon/src/autostart.h`           | systemd user 🔜   | LaunchAgent 🔜    | logon Scheduled Task 🔜     |

✅ implemented · 🔜 planned

## Open decision (gates Phases 5–6)

**Windows: logon Scheduled Task vs. Windows Service.** A Service survives logout
but runs in session 0 and must use `CreateProcessAsUser` to launch GUI client
instances on the user's desktop. A logon task runs in the user session (GUI
instances "just work") but dies at logout. Default: **logon task** until a
"survive logout on Windows" requirement actually appears.
