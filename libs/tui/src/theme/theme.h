#pragma once

#include <ftxui/dom/elements.hpp>

namespace hestia::tui {
    // Visual styling for the TUI as semantic *roles*, not fixed colors. Each role
    // is an ftxui::Decorator built only from terminal-honoring primitives (bold/
    // dim/inverted), so the UI inherits the user's terminal theme — no palette of
    // our own. Layouts and components pull styling from these roles, never a color.
    struct Theme {
        // Branding / primary headings.
        ftxui::Decorator brand = ftxui::bold;
        // Emphasised text (active titles, key captions).
        ftxui::Decorator emphasis = ftxui::bold;
        // De-emphasised text (hints, secondary labels).
        ftxui::Decorator muted = ftxui::dim;
        // Selected / highlighted row — swaps fg/bg using the terminal's own
        // colors rather than imposing ours.
        ftxui::Decorator selected = ftxui::inverted;
        // Plain body text: identity, i.e. the terminal default.
        ftxui::Decorator normal = ftxui::nothing;
    };
}
