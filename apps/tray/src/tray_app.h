#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <hestia/client/client.h>

#include "tray_backend.h"

namespace hestia::tray {
    // The platform-neutral tray helper. It holds one client connection to the
    // daemon, seeds its state, subscribes to the live event stream, and projects
    // the result into a TrayModel that the platform backend renders. The backend
    // owns the UI loop; this class owns the daemon-facing logic.
    class TrayApp {
    public:
        TrayApp();

        // Seed state, render the first model, subscribe, then run the UI loop.
        int run();

    private:
        // Pull the current process list and autostart state. Runs on the main
        // thread before the event subscription begins, so it may call the daemon.
        void seed_state();

        // Handle one pushed event. Runs on the client's reader thread, so it must
        // not issue daemon calls (that would deadlock the reader) — it only
        // updates the cache from the event payload and re-renders.
        void on_event(const client::ProcessEvent &event);

        // Recompute the TrayModel from cached state and hand it to the backend.
        void rebuild_model();

        // Flip the daemon's autostart registration. Runs on the UI thread (a menu
        // click), so calling the daemon here is safe.
        void toggle_autostart();

        client::Client client_;
        std::unique_ptr<TrayBackend> backend_;

        std::mutex mu_;
        std::map<std::string, std::string> states_; // process id -> state
        bool autostart_enabled_ = false;
    };
}
