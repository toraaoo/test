#pragma once

#include "command.h"

namespace hestia::cli {
    // `hestia autostart` — a command group nesting `enable`, `disable`, and
    // `status` leaf commands that drive the daemon's autostart registration.
    class AutostartCommand : public CommandGroup {
    public:
        AutostartCommand();
    };
}
