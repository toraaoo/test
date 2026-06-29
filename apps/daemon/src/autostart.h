#pragma once

#include <memory>

// The Autostart seam (Phase 5): register/unregister the daemon to start with the
// user session. Each platform implements it with its native mechanism — systemd
// user unit (Linux), LaunchAgent (macOS), logon Scheduled Task (Windows; a
// Windows Service is the deferred alternative for surviving logout). All platform
// divergence lives behind this interface, mirroring ProcessSupervisor and
// IpcTransport.
namespace hestia::daemon {
    class Autostart {
    public:
        virtual ~Autostart() = default;

        // Install/remove the autostart registration. Idempotent — enabling when
        // already enabled (or disabling when absent) is a no-op, not an error.
        virtual void enable() = 0;
        virtual void disable() = 0;

        virtual bool is_enabled() const = 0;
    };

    // Construct the platform autostart manager. The registration points at the
    // running daemon's own executable, resolved from the OS, so it survives moves
    // of the build tree. Throws on an unsupported platform.
    std::unique_ptr<Autostart> make_autostart();
}
