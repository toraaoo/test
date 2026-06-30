#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

#include "process_types.h"

// The ProcessSpawner seam: launch a process as a child of the daemon with its
// stdout/stderr redirected to a log file at the OS level. Uses raw OS primitives
// (fork/exec on POSIX, CreateProcess on Windows), isolated so it can be replaced
// per platform and mocked in tests. Each process leads its own group (process
// group / Job Object) so terminate() takes down the whole tree.
namespace hestia::daemon {
    // The outcome of a finished child: its exit code, and whether a signal (POSIX)
    // killed it rather than a normal exit.
    struct ProcessExit {
        int code = 0;
        bool signaled = false;
    };

    class ProcessSpawner {
    public:
        virtual ~ProcessSpawner() = default;

        // Spawn `spec`'s program as a child of the daemon, appending its output to
        // `log`. Returns the new process's pid. Throws std::runtime_error if the
        // launch fails.
        virtual std::int64_t spawn(const LaunchSpec &spec,
                                   const std::filesystem::path &log) = 0;

        // Reap a child launched by THIS spawner instance, without blocking.
        // Returns its exit outcome if it has exited — consuming the zombie so the
        // pid is freed — or nullopt if it is still running, or was not launched by
        // this spawner (e.g. a process re-adopted after a daemon restart; check
        // those for liveness via the LivenessProbe instead).
        virtual std::optional<ProcessExit> reap(std::int64_t pid) = 0;

        // Request termination of a launched process. Best-effort: a pid that is
        // already gone is not an error.
        virtual void terminate(std::int64_t pid) = 0;
    };

    // The platform process spawner.
    std::unique_ptr<ProcessSpawner> make_process_spawner();
}
