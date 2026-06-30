#include "layout/status_bar.h"

#include "app_context.h"
#include "components/key_hint.h"
#include "theme/theme.h"

namespace hestia::tui {
    ftxui::Element status_bar(const AppContext &ctx) {
        using namespace ftxui;
        return hbox({
            text(" "),
            key_hints({{"up/down", "navigate"},
                       {"tab", "focus actions"},
                       {"enter", "activate"},
                       {"q", "quit"}},
                      *ctx.theme),
            filler(),
        });
    }
}
