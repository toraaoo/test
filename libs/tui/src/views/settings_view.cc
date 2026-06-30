#include "views/settings_view.h"

#include <exception>
#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <spdlog/spdlog.h>

#include <hestia/client/client.h>

#include "app_context.h"
#include "components/button.h"
#include "components/panel.h"
#include "components/text_field.h"
#include "theme/theme.h"

namespace hestia::tui {
    RouteId SettingsView::id() const {
        return "settings";
    }

    std::string SettingsView::title() const {
        return "Settings";
    }

    void SettingsView::load(AppContext &ctx) {
        if (!ctx.client) return;
        try {
            autostart_enabled_ = ctx.client->autostart_status();
            autostart_known_ = true;
        } catch (const std::exception &e) {
            autostart_error_ = e.what();
            spdlog::warn("tui: settings could not read autostart status: {}", e.what());
        }
    }

    ftxui::Component SettingsView::build(AppContext &ctx) {
        using namespace ftxui;
        load(ctx);

        auto key_field = text_field(&key_, "key", *ctx.theme);
        auto value_field = text_field(&value_, "value", *ctx.theme);

        auto get_button = pill_button(
            "Get",
            [this, &ctx] {
                if (!ctx.client) return;
                try {
                    value_ = ctx.client->config_get(key_).value_or("");
                    config_status_ = value_.empty() ? "key not set" : "loaded";
                } catch (const std::exception &e) {
                    config_status_ = e.what();
                }
            },
            *ctx.theme);

        auto set_button = pill_button(
            "Set",
            [this, &ctx] {
                if (!ctx.client) return;
                try {
                    ctx.client->config_set(key_, value_);
                    config_status_ = "saved";
                } catch (const std::exception &e) {
                    config_status_ = e.what();
                }
            },
            *ctx.theme);

        auto toggle_button = pill_button(
            "Toggle",
            [this, &ctx] {
                if (!ctx.client) return;
                try {
                    if (autostart_enabled_)
                        ctx.client->autostart_disable();
                    else
                        ctx.client->autostart_enable();
                    autostart_enabled_ = !autostart_enabled_;
                    autostart_known_ = true;
                    autostart_error_.clear();
                } catch (const std::exception &e) {
                    autostart_error_ = e.what();
                }
            },
            *ctx.theme);

        auto container = Container::Vertical(
            {key_field, value_field, get_button, set_button, toggle_button});

        return Renderer(container, [this, &ctx, key_field, value_field, get_button,
                                    set_button, toggle_button] {
            const Theme &theme = *ctx.theme;
            const int label_w = 7;
            const int field_w = 28;

            auto grid_row = [&](const std::string &label, Element field) {
                return hbox({
                    text(label) | theme.muted | size(WIDTH, EQUAL, label_w),
                    field | size(WIDTH, EQUAL, field_w),
                });
            };

            Elements body;
            if (!ctx.client) {
                body.push_back(text("daemon unavailable") | theme.emphasis);
                body.push_back(filler());
                return panel("Settings", vbox(std::move(body)), theme) | flex;
            }

            body.push_back(text("Config") | theme.emphasis);
            body.push_back(grid_row("key", key_field->Render()));
            body.push_back(grid_row("value", value_field->Render()));
            body.push_back(hbox({
                text("") | size(WIDTH, EQUAL, label_w),
                get_button->Render(),
                text("  "),
                set_button->Render(),
            }));
            if (!config_status_.empty())
                body.push_back(grid_row("", text(config_status_) | theme.muted));

            body.push_back(separatorEmpty());

            const std::string state =
                autostart_known_ ? (autostart_enabled_ ? "enabled" : "disabled") : "unknown";
            body.push_back(text("Autostart") | theme.emphasis);
            body.push_back(grid_row("login", text(state) | theme.normal));
            body.push_back(hbox({
                text("") | size(WIDTH, EQUAL, label_w),
                toggle_button->Render(),
            }));
            if (!autostart_error_.empty())
                body.push_back(grid_row("", text(autostart_error_) | theme.muted));

            body.push_back(filler());
            return panel("Settings", vbox(std::move(body)), theme) | flex;
        });
    }
}
