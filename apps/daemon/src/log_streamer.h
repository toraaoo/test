#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

// Per-process log tailing: tracks a read offset per process so the supervision
// loop can stream only newly-appended bytes, and reads the last N lines on
// demand. Pure file I/O, no OS-specific calls — testable against temp files.
// See P3 of the refactor.
namespace hestia::daemon {
    class LogStreamer {
    public:
        // Start streaming a process's log from its current end, so historical
        // output (available via tail) is not re-emitted on subscribe/adopt.
        void reset(const std::string &id, const std::filesystem::path &path);

        // Drop a process's tracked offset.
        void forget(const std::string &id);

        // Return bytes appended to `path` since the last read for `id` (advancing
        // the offset), or empty if there is nothing new. A shrunk file (rotation
        // or truncation) restarts from its beginning.
        std::string read_new(const std::string &id, const std::filesystem::path &path);

        // The last `max_lines` lines of `path`, or empty if it cannot be read.
        static std::string tail(const std::filesystem::path &path, int max_lines);

    private:
        std::map<std::string, std::uint64_t> offsets_;
    };
}
