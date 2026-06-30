#include "views/home_view.h"

#include <exception>
#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <spdlog/spdlog.h>

#include "app_context.h"
#include "components/button.h"
#include "components/panel.h"
#include "components/text_field.h"
#include "theme/theme.h"

namespace hestia::tui {
    RouteId HomeView::id() const {
        return "home";
    }

    std::string HomeView::title() const {
        return "Overview";
    }

    void HomeView::load(AppContext &ctx) {
        if (!ctx.client) return;
        try {
            info_ = ctx.client->app_info();
            connected_ = true;
        } catch (const std::exception &e) {
            error_ = e.what();
            spdlog::warn("tui: overview could not read app info: {}", e.what());
        }
    }

    ftxui::Component HomeView::build(AppContext &ctx) {
        using namespace ftxui;
        load(ctx);

        auto name_field = text_field(&name_, "name", *ctx.theme);
        auto greet_button = pill_button(
            "Greet",
            [this, &ctx] {
                if (!ctx.client) {
                    greet_error_ = "daemon unavailable";
                    return;
                }
                try {
                    greeting_ = ctx.client->greet(name_);
                    greet_error_.clear();
                } catch (const std::exception &e) {
                    greet_error_ = e.what();
                }
            },
            *ctx.theme);
        auto quit_button = pill_button("Quit", ctx.request_quit, *ctx.theme);

        auto container = Container::Vertical({name_field, greet_button, quit_button});

        return Renderer(container, [this, &ctx, name_field, greet_button, quit_button] {
            const Theme &theme = *ctx.theme;

            auto row = [&](const std::string &label, const std::string &value) {
                return hbox({
                    text(label) | theme.muted | size(WIDTH, EQUAL, 9),
                    text(value) | theme.normal,
                });
            };

            Elements body;
            if (connected_) {
                body.push_back(text(info_.name + " " + info_.version) | theme.brand);
                body.push_back(row("channel", info_.channel));
                body.push_back(row("vendor", info_.vendor));
                body.push_back(row("id", info_.id));
            } else if (!ctx.client) {
                body.push_back(text("daemon unavailable") | theme.emphasis);
                body.push_back(text("start it with: hestiad serve") | theme.muted);
            } else {
                body.push_back(text("daemon error") | theme.emphasis);
                body.push_back(text(error_) | theme.muted);
            }

            body.push_back(separatorEmpty());
            body.push_back(text("Greet") | theme.emphasis);
            body.push_back(hbox({
                name_field->Render() | size(WIDTH, EQUAL, 24),
                text("  "),
                greet_button->Render(),
            }));
            if (!greeting_.empty()) body.push_back(text(greeting_) | theme.normal);
            if (!greet_error_.empty()) body.push_back(text(greet_error_) | theme.muted);

            body.push_back(filler());
            body.push_back(quit_button->Render());

            return panel("Overview", vbox(std::move(body)), theme) | flex;
        });
    }
}
