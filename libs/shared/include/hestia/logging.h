#pragma once

#include <filesystem>

namespace hestia {
    enum class LogLevel {
        trace,
        debug,
        info,
        warn,
        error,
        critical,
        off,
    };

    // Configure the process-wide logger (output pattern and minimum level). Call
    // once at application startup, before any logging happens. If `file` is given,
    // logs also go to that path via a size-rotated sink — used by the daemon, whose
    // stderr is detached to /dev/null, so its logs would otherwise be lost.
    void init_logging(LogLevel level = LogLevel::info,
                      const std::filesystem::path &file = {});
}
