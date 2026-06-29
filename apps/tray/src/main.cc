#include <exception>
#include <iostream>

#include "tray_app.h"

// Hestia tray helper — a thin client frontend that shows the daemon's status in
// the system tray and lets the user toggle login autostart. It owns no launcher
// state; it drives the daemon over the client SDK like every other frontend.
int main() {
    try {
        hestia::tray::TrayApp app;
        return app.run();
    } catch (const std::exception &e) {
        std::cerr << "hestia-tray: " << e.what() << '\n';
        return 1;
    }
}
