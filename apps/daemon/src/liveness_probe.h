#pragma once

#include <cstdint>
#include <memory>

#include "process_types.h"

// The LivenessProbe seam: "is this pid still ours?". Isolated so the OS-specific
// mechanism (Linux /proc + kill(0) today; pidfd/kqueue/Job Objects later) can be
// swapped or faked in tests without touching the supervisor. See P3 of the
// refactor and the cross-platform seam table in docs/daemon-protocol.md.
namespace hestia::daemon {
    class LivenessProbe {
    public:
        virtual ~LivenessProbe() = default;

        // Is a process with this pid currently alive?
        virtual bool is_alive(std::int64_t pid) const = 0;

        // An opaque, monotonic-per-process start time used to disambiguate PID
        // reuse. 0 means "unavailable on this platform".
        virtual std::int64_t read_start_time(std::int64_t pid) const = 0;

        // A record is still the process we launched only if its pid is alive AND
        // its start time matches what we recorded — guarding against a different
        // process having since reused the pid.
        bool matches(const ProcessRecord &rec) const {
            return is_alive(rec.pid) &&
                   (rec.start_time == 0 || read_start_time(rec.pid) == rec.start_time);
        }
    };

    // The platform liveness probe.
    std::unique_ptr<LivenessProbe> make_liveness_probe();
}
