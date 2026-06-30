#pragma once

#include <string>
#include <vector>

#include "navigation/route.h"

namespace hestia::tui {
    class View;

    // Identifier for a modal overlay (e.g. "confirm_quit"). Like a RouteId, but
    // for transient layers stacked above the active view.
    using OverlayId = std::string;

    // The router. Owns the active-route selection (shared with the sidebar Menu
    // and the content Tab via a single int) and the current modal overlay. Fires
    // view lifecycle hooks on navigation. Holds non-owning view pointers; the
    // views themselves are owned by the shell.
    class Navigator {
    public:
        explicit Navigator(std::vector<View *> views);

        // The selected index, shared by reference into the Menu and the Tab.
        int &selected();
        View *active() const;
        // Navigate to a route by id; no-op (with a warning) if unknown.
        void goto_route(const RouteId &id);
        void next();
        void prev();

        // Fire the initial view's on_enter. Call once before the loop starts.
        void start();
        // Detect a selection change and fire on_exit/on_enter. Call once per
        // frame (selection can change via the Menu, which mutates the shared int
        // directly, bypassing goto_route).
        void tick();

        void open_overlay(OverlayId id);
        void close_overlay();
        bool has_overlay() const;
        const OverlayId &active_overlay() const;

    private:
        std::vector<View *> views_;
        int selected_ = 0;
        int last_ = 0;
        OverlayId overlay_;
    };
}
