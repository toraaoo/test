#include <exception>
#include <iostream>

#include "single_instance.h"
#include "tray_app.h"

// Hestia tray helper — a thin client frontend that shows the daemon's status in
// the system tray and lets the user toggle login autostart. It owns no launcher
// state; it drives the daemon over the client SDK like every other frontend.
int main() {
    // Only one tray per user session; a second invocation steps aside quietly.
    hestia::tray::SingleInstance instance;
    if (!instance.primary()) {
        std::cerr << "hestia-tray: already running\n";
        return 0;
    }

    try {
        hestia::tray::TrayApp app;
        return app.run();
    } catch (const std::exception &e) {
        std::cerr << "hestia-tray: " << e.what() << '\n';
        return 1;
    }
}
