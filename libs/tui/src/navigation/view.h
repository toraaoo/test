#pragma once

#include <string>

#include <ftxui/component/component_base.hpp>

#include "layout/layout.h"
#include "navigation/route.h"

namespace hestia::tui {
    struct AppContext;

    // A route-level screen: owns its component subtree and declares which layout
    // arranges it (like a React page composed from components/). Adding one = a
    // file here + a line in make_views().
    class View {
    public:
        virtual ~View() = default;

        // Stable route identifier (e.g. "home"). Addressed by the Navigator.
        virtual RouteId id() const = 0;

        // Label shown in the navigation sidebar.
        virtual std::string title() const = 0;

        // Which layout arranges this view. Defaults to the sidebar shell; a view
        // swaps it with a one-line override (e.g. Centered for a wizard).
        virtual LayoutId layout() const { return layout::Sidebar; }

        // Build the view's component tree. Called once during startup; shared
        // services arrive via ctx (props-down). State is plain member fields.
        virtual ftxui::Component build(AppContext &ctx) = 0;

        // Lifecycle hooks, fired by the Navigator on navigation (mount/unmount).
        virtual void on_enter() {}
        virtual void on_exit() {}
    };
}
