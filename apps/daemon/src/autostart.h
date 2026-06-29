#pragma once

// The Autostart seam (Phase 5): register/unregister the daemon to start with the
// user session. Each platform implements it with its native mechanism — systemd
// user unit (Linux), LaunchAgent (macOS), logon Scheduled Task (Windows; a
// Windows Service is the deferred alternative for surviving logout).
namespace hestia::daemon {
    class Autostart {
    public:
        virtual ~Autostart() = default;

        // Install/remove the autostart registration. Idempotent.
        virtual void enable() = 0;
        virtual void disable() = 0;

        virtual bool is_enabled() const = 0;
    };
}
