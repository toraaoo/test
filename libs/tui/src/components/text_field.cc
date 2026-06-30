#include "components/text_field.h"

#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>

#include "theme/theme.h"

namespace hestia::tui {
    ftxui::Component text_field(std::string *content, std::string placeholder,
                                const Theme &theme) {
        using namespace ftxui;

        InputOption option;
        option.placeholder = std::move(placeholder);
        option.multiline = false;
        option.transform = [&theme](InputState state) {
            Element body = state.is_placeholder ? state.element | theme.muted : state.element;
            // Underline marks the field; focus only brightens it — no fill/invert.
            return body | underlined | (state.focused ? theme.normal : theme.muted);
        };
        return Input(content, option);
    }
}
