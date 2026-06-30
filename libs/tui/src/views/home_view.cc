#include "views/home_view.h"

#include <exception>
#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <spdlog/spdlog.h>

#include "app_context.h"
#include "components/button.h"
#include "components/panel.h"
#include "theme/theme.h"

namespace hestia::tui {
    RouteId HomeView::id() const {
        return "home";
    }

    std::string HomeView::title() const {
        return "Home";
    }

    void HomeView::load(AppContext &ctx) {
        if (!ctx.client) return;
        try {
            info_ = ctx.client->app_info();
            greeting_ = ctx.client->greet("");
            connected_ = true;
        } catch (const std::exception &e) {
            error_ = e.what();
            spdlog::warn("tui: home view could not read daemon state: {}", e.what());
        }
    }

    ftxui::Component HomeView::build(AppContext &ctx) {
        using namespace ftxui;
        load(ctx);

        auto quit_button = pill_button("Quit", ctx.request_quit, *ctx.theme);
        auto container = Container::Vertical({quit_button});

        return Renderer(container, [quit_button, &ctx, this] {
            const Theme &theme = *ctx.theme;

            auto field = [&](const std::string &label, const std::string &value) {
                return hbox({
                    text(label) | theme.muted | size(WIDTH, EQUAL, 10),
                    text(value) | theme.normal,
                });
            };

            Elements rows;
            if (connected_) {
                rows.push_back(text(greeting_) | theme.emphasis);
                rows.push_back(text(""));
                rows.push_back(field("name", info_.name));
                rows.push_back(field("version", info_.version));
                rows.push_back(field("channel", info_.channel));
                rows.push_back(field("vendor", info_.vendor));
            } else if (!ctx.client) {
                rows.push_back(text("daemon unavailable") | theme.muted);
                rows.push_back(text("start it with: hestiad serve") | theme.muted);
            } else {
                rows.push_back(text("daemon error: " + error_) | theme.muted);
            }
            rows.push_back(filler());
            rows.push_back(quit_button->Render() | hcenter);

            return panel("Home", vbox(std::move(rows)), theme) | flex;
        });
    }
}
