#pragma once

#include <string>

#include <hestia/client/client.h>

#include "navigation/view.h"

namespace hestia::tui {
    // Landing view: the daemon's identity and greeting, read live over the SDK.
    class HomeView : public View {
    public:
        RouteId id() const override;
        std::string title() const override;
        ftxui::Component build(AppContext &ctx) override;

    private:
        void load(AppContext &ctx);

        bool connected_ = false;
        std::string greeting_;
        std::string error_;
        client::AppInfo info_;
    };
}
