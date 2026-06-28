#include "tui.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

namespace hestia::cli {
    int run_tui() {
        using namespace ftxui;

        auto screen = ScreenInteractive::Fullscreen();

        auto quit_button = Button("Quit", screen.ExitLoopClosure());

        // Feature components get added to this container.
        auto layout = Container::Vertical(
            {
                quit_button,
            }
        );

        auto renderer = Renderer(
            layout, [&] {
                return vbox({
                           text("Hestia") | bold | hcenter,
                           separator(),
                           text("Welcome to the Hestia terminal UI.") | hcenter,
                           filler(),
                           quit_button->Render() | hcenter,
                       }) | border;
            }
        );

        screen.Loop(renderer);
        return 0;
    }
}
