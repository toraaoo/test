#pragma once

#include <string>

#include <ftxui/component/component_base.hpp>

namespace hestia::tui {
    struct Theme;

    // A single-line text input with a clean, borderless look: a leading prompt
    // and an underline that brightens on focus (no boxy frame). The caller owns
    // the backing string and arranges width.
    ftxui::Component text_field(std::string *content, std::string placeholder,
                                const Theme &theme);
}
