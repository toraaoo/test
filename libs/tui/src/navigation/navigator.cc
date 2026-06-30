#include "navigation/navigator.h"

#include <utility>

#include <spdlog/spdlog.h>

#include "navigation/view.h"

namespace hestia::tui {
    Navigator::Navigator(std::vector<View *> views) : views_(std::move(views)) {}

    int &Navigator::selected() {
        return selected_;
    }

    View *Navigator::active() const {
        // selected_ is shared by reference with the FTXUI menu/tab, so guard the
        // range rather than trust it — an out-of-range index would be UB.
        if (selected_ < 0 || selected_ >= static_cast<int>(views_.size())) return nullptr;
        return views_[selected_];
    }

    void Navigator::goto_route(const RouteId &id) {
        for (int i = 0; i < static_cast<int>(views_.size()); ++i) {
            if (views_[i]->id() == id) {
                selected_ = i;
                return;
            }
        }
        spdlog::warn("tui: goto_route('{}') — no such route; staying put", id);
    }

    void Navigator::next() {
        if (views_.empty()) return;
        selected_ = (selected_ + 1) % static_cast<int>(views_.size());
    }

    void Navigator::prev() {
        if (views_.empty()) return;
        const int n = static_cast<int>(views_.size());
        selected_ = (selected_ - 1 + n) % n;
    }

    void Navigator::start() {
        if (View *v = active()) v->on_enter();
        last_ = selected_;
    }

    void Navigator::tick() {
        if (selected_ == last_) return;
        if (last_ >= 0 && last_ < static_cast<int>(views_.size()))
            views_[last_]->on_exit();
        if (View *v = active()) v->on_enter();
        last_ = selected_;
    }

    void Navigator::open_overlay(OverlayId id) {
        overlay_ = std::move(id);
    }

    void Navigator::close_overlay() {
        overlay_.clear();
    }

    bool Navigator::has_overlay() const {
        return !overlay_.empty();
    }

    const OverlayId &Navigator::active_overlay() const {
        return overlay_;
    }
}
