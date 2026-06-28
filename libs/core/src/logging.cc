#include <hestia/logging.h>

#include <spdlog/spdlog.h>

namespace hestia {
    namespace {
        spdlog::level::level_enum to_spdlog(LogLevel level) {
            switch (level) {
                case LogLevel::trace:    return spdlog::level::trace;
                case LogLevel::debug:    return spdlog::level::debug;
                case LogLevel::info:     return spdlog::level::info;
                case LogLevel::warn:     return spdlog::level::warn;
                case LogLevel::error:    return spdlog::level::err;
                case LogLevel::critical: return spdlog::level::critical;
                case LogLevel::off:      return spdlog::level::off;
            }
            return spdlog::level::info;
        }
    }

    void init_logging(LogLevel level) {
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        spdlog::set_level(to_spdlog(level));
    }
}
