#include <hestia/logging.h>

#include <memory>
#include <system_error>
#include <vector>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
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

    void init_logging(LogLevel level, const std::filesystem::path &file) {
        std::vector<spdlog::sink_ptr> sinks;
        sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());

        if (!file.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(file.parent_path(), ec);
            // Keep a small, bounded history: 1 MiB per file, three rotations.
            sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                file.string(), 1024 * 1024, 3));
        }

        auto logger = std::make_shared<spdlog::logger>("hestia", sinks.begin(), sinks.end());
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        logger->set_level(to_spdlog(level));
        spdlog::set_default_logger(std::move(logger));
    }
}
