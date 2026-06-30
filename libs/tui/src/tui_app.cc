#include "hestia/tui/run.h"

#include <exception>
#include <optional>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <hestia/client/client.h>
#include <spdlog/spdlog.h>

#include "app_context.h"
#include "input/global_keys.h"
#include "layout/header_bar.h"
#include "layout/layout.h"
#include "layout/layout_registry.h"
#include "layout/sidebar.h"
#include "layout/status_bar.h"
#include "navigation/navigator.h"
#include "navigation/view.h"
#include "navigation/view_registry.h"
#include "overlays/confirm_quit.h"
#include "theme/theme.h"

namespace hestia::tui {
    int run() {
        using namespace ftxui;

        auto screen = ScreenInteractive::Fullscreen();

        Theme theme;

        // Views are owned here; the Navigator routes over non-owning pointers.
        auto views = make_views();
        std::vector<View *> view_ptrs;
        std::vector<std::string> titles;
        view_ptrs.reserve(views.size());
        titles.reserve(views.size());
        for (auto &v : views) {
            view_ptrs.push_back(v.get());
            titles.push_back(v->title());
        }
        Navigator nav(std::move(view_ptrs));

        std::optional<client::Client> client;
        try {
            client = client::Client::connect();
        } catch (const std::exception &e) {
            spdlog::warn("tui: daemon unreachable: {}", e.what());
        }

        // Shared context (props-down to every view).
        AppContext ctx;
        ctx.theme = &theme;
        ctx.nav = &nav;
        ctx.client = client ? &*client : nullptr;
        ctx.exit_app = screen.ExitLoopClosure();
        ctx.request_quit = [&nav] { nav.open_overlay(overlay::ConfirmQuit); };

        // Build all interactive components ONCE: the per-view panels (one Tab),
        // the sidebar menu, and the overlay. The selection int is shared by the
        // menu and the tab, so navigating the menu swaps the active view.
        Components panels;
        panels.reserve(views.size());
        for (auto &v : views)
            panels.push_back(v->build(ctx));
        auto content = Container::Tab(panels, &nav.selected());
        auto sidebar = make_sidebar(&titles, &nav.selected(), theme);
        auto overlay = make_confirm_quit(ctx);

        auto layouts = make_layouts();

        // Event/focus routing: a two-entry Tab sends input to exactly one layer —
        // the main pane normally, the overlay while it is shown. This is the
        // component tree (built once); the layout below only arranges Elements.
        auto main_pane = Container::Horizontal({sidebar, content});
        int focus_layer = 0; // 0 = main pane, 1 = overlay
        auto event_root = Container::Tab({main_pane, overlay}, &focus_layer);

        // Per-frame render: re-read state, route focus, and let the active view's
        // layout arrange the slots. State is plain variables; there is no
        // re-render diff (FTXUI is not reactive).
        auto renderer = Renderer(event_root, [&] {
            nav.tick(); // fire on_exit/on_enter if the menu changed the selection
            focus_layer = nav.has_overlay() ? 1 : 0;

            View *active = nav.active();
            const LayoutId id = active ? active->layout() : layout::Sidebar;

            LayoutSlots slots;
            slots.content = content->Render();
            slots.header = header_bar(ctx, active ? active->title() : "");
            slots.sidebar = sidebar->Render();
            slots.status = status_bar(ctx);
            if (nav.has_overlay())
                slots.overlay = overlay->Render();

            return layouts.get(id).arrange(slots);
        });

        auto root = with_global_keys(renderer, ctx);

        nav.start(); // initial on_enter
        screen.Loop(root);
        return 0;
    }
}
