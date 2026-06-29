#include "liveness_probe.h"

#include <stdexcept>

#if !defined(_WIN32)
#include <cerrno>
#include <csignal>
#endif

#if defined(__linux__)
#include <fstream>
#include <sstream>
#include <string>
#endif

namespace hestia::daemon {
    namespace {
#if defined(__linux__)
        // Field 22 of /proc/<pid>/stat is the process start time (clock ticks
        // since boot). Combined with the pid it survives PID reuse: a different
        // process that later reuses the pid will have a different start time.
        std::int64_t linux_start_time(std::int64_t pid) {
            std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
            if (!f) return 0;
            std::string content((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
            // comm (field 2) is parenthesised and may contain spaces — start
            // parsing after the last ')'.
            const auto close = content.rfind(')');
            if (close == std::string::npos) return 0;
            std::istringstream rest(content.substr(close + 1));
            std::string token;
            // After comm, the next token is field 3 (state); start time is field
            // 22, i.e. the 20th token from here.
            for (int i = 0; i < 20; ++i) {
                if (!(rest >> token)) return 0;
            }
            try {
                return std::stoll(token);
            } catch (...) {
                return 0;
            }
        }
#endif

#if !defined(_WIN32)
        class PosixLivenessProbe final : public LivenessProbe {
        public:
            bool is_alive(std::int64_t pid) const override {
                if (pid <= 0) return false;
                if (::kill(static_cast<pid_t>(pid), 0) == 0) return true;
                return errno == EPERM; // exists but not ours to signal
            }

            std::int64_t read_start_time(std::int64_t pid) const override {
#if defined(__linux__)
                return linux_start_time(pid);
#else
                (void) pid;
                return 0; // start-time disambiguation is a Linux refinement for now
#endif
            }
        };
#endif
    }

    std::unique_ptr<LivenessProbe> make_liveness_probe() {
#if !defined(_WIN32)
        return std::make_unique<PosixLivenessProbe>();
#else
        throw std::runtime_error("liveness probing is not yet implemented on Windows");
#endif
    }
}
