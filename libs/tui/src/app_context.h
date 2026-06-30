#pragma once

#include <functional>

namespace hestia::client {
    class Client;
}

namespace hestia::tui {
    class Navigator;
    struct Theme;

    // State and services shared with every view and with the shell. Passed by
    // reference into View::build (mirrors a React <Context.Provider>). Shared
    // state lives here; everything else is component-local.
    struct AppContext {
        client::Client *client = nullptr;

        // Ask the app to quit. Routes through the confirm-quit overlay rather
        // than exiting immediately.
        std::function<void()> request_quit;
        // Exit the event loop unconditionally (the screen's loop-exit closure).
        std::function<void()> exit_app;

        // Navigation + overlay control.
        Navigator *nav = nullptr;
        // Styling roles (terminal-honoring; see theme/theme.h).
        const Theme *theme = nullptr;
    };
}
