#pragma once

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

    // Configure the process-wide logger (output pattern and minimum level).
    // Call once at application startup, before any logging happens.
    void init_logging(LogLevel level = LogLevel::info);
}
