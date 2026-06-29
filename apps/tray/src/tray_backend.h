#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

// The tray-icon seam. The platform-neutral TrayApp builds a TrayModel — a
// tooltip and a flat menu — and hands it to a TrayBackend, which renders it with
// the OS-native mechanism (StatusNotifierItem via AppIndicator on Linux,
// NSStatusItem on macOS, Shell_NotifyIcon on Windows). All GUI-toolkit divergence
// lives behind this interface, mirroring the daemon's ProcessSupervisor/Autostart
// seams.
namespace hestia::tray {
    // One entry in the tray menu. A separator ignores every other field; an item
    // with a null `on_click` is informational (typically disabled). `checked`
    // renders a checkmark for toggle items.
    struct MenuItem {
        std::string label;
        bool separator = false;
        bool enabled = true;
        bool checked = false;
        std::function<void()> on_click; // invoked on the UI thread; may be null
    };

    // A separator menu entry.
    inline MenuItem separator() {
        MenuItem item;
        item.separator = true;
        return item;
    }

    // The full visual state: the icon tooltip and the menu shown on click.
    struct TrayModel {
        std::string tooltip;
        std::vector<MenuItem> items;
    };

    class TrayBackend {
    public:
        virtual ~TrayBackend() = default;

        // Replace the tooltip and menu. Safe to call from any thread; the backend
        // marshals the update onto its UI thread.
        virtual void set_model(TrayModel model) = 0;

        // Enter the platform UI loop. Blocks until quit() is called.
        virtual void run() = 0;

        // Ask the UI loop to exit. Safe to call from any thread.
        virtual void quit() = 0;
    };

    // Construct the platform tray backend, labelled with the application name.
    std::unique_ptr<TrayBackend> make_tray_backend(std::string app_name);
}
