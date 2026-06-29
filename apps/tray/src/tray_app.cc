#include "tray_app.h"

#include <string>
#include <utility>

#include <hestia/app_info.h>

namespace hestia::tray {
    TrayApp::TrayApp()
        : client_(client::Client::connect()),
          backend_(make_tray_backend(APP_NAME)) {}

    int TrayApp::run() {
        seed_state();
        rebuild_model();
        client_.subscribe([this](const client::ProcessEvent &event) { on_event(event); });
        backend_->run();
        return 0;
    }

    void TrayApp::seed_state() {
        std::lock_guard<std::mutex> lk(mu_);
        try {
            for (const auto &p: client_.process_list()) states_[p.id] = p.state;
        } catch (const std::exception &) {
            // A daemon hiccup at startup is not fatal: render what we have and let
            // the event stream fill in the rest.
        }
        try {
            autostart_enabled_ = client_.autostart_status();
        } catch (const std::exception &) {
        }
    }

    void TrayApp::on_event(const client::ProcessEvent &event) {
        if (event.topic != "process.state" || !event.process) return; // ignore log chunks
        {
            std::lock_guard<std::mutex> lk(mu_);
            states_[event.process->id] = event.process->state;
        }
        rebuild_model();
    }

    void TrayApp::rebuild_model() {
        TrayModel model;
        {
            std::lock_guard<std::mutex> lk(mu_);

            std::size_t running = 0;
            for (const auto &[id, state]: states_) {
                if (state == "running") ++running;
            }
            model.tooltip = std::string(APP_NAME) + " — " + std::to_string(running) + " running";

            MenuItem header;
            header.label = std::string(APP_NAME) + " daemon";
            header.enabled = false;
            model.items.push_back(std::move(header));
            model.items.push_back(separator());

            if (states_.empty()) {
                MenuItem none;
                none.label = "No processes";
                none.enabled = false;
                model.items.push_back(std::move(none));
            } else {
                for (const auto &[id, state]: states_) {
                    MenuItem item;
                    item.label = id + ": " + state;
                    item.enabled = false;
                    model.items.push_back(std::move(item));
                }
            }

            model.items.push_back(separator());

            MenuItem autostart;
            autostart.label = "Start at login";
            autostart.checked = autostart_enabled_;
            autostart.on_click = [this] { toggle_autostart(); };
            model.items.push_back(std::move(autostart));

            model.items.push_back(separator());

            MenuItem quit;
            quit.label = "Quit";
            quit.on_click = [this] { backend_->quit(); };
            model.items.push_back(std::move(quit));
        }
        backend_->set_model(std::move(model));
    }

    void TrayApp::toggle_autostart() {
        bool target;
        {
            std::lock_guard<std::mutex> lk(mu_);
            target = !autostart_enabled_;
        }
        try {
            if (target) {
                client_.autostart_enable();
            } else {
                client_.autostart_disable();
            }
            std::lock_guard<std::mutex> lk(mu_);
            autostart_enabled_ = target;
        } catch (const std::exception &) {
            // Leave the cached state unchanged if the daemon rejected the change.
        }
        rebuild_model();
    }
}
