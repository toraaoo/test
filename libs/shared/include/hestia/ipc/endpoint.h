#pragma once

#include <filesystem>

// Endpoint (socket/pipe path) resolution for the daemon transport. This is a
// transport concern, not a data concern: the runtime dir holds the ephemeral
// socket and is distinct from hestia_engine's persistent data_home.
namespace hestia::ipc {
    // The per-user runtime directory for ephemeral transport state.
    //   Linux/macOS: $XDG_RUNTIME_DIR/hestia, falling back to /tmp/hestia-<uid>.
    //   Windows:     resolved against the named-pipe namespace (planned).
    std::filesystem::path runtime_dir();

    // The default daemon endpoint: runtime_dir()/"hestiad.sock" on POSIX. Both
    // the daemon (bind) and clients (connect) resolve the same path here, so the
    // location lives in exactly one place.
    std::filesystem::path default_endpoint();
}
