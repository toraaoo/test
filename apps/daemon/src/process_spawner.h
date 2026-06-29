#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include "process_types.h"

// The ProcessSpawner seam: launch a process detached from the daemon, with its
// stdout/stderr redirected to a log file at the OS level. This is the part that
// must use raw OS primitives (double-fork/exec on POSIX), isolated so it can be
// replaced per platform and mocked in tests. See P3 of the refactor.
namespace hestia::daemon {
    class ProcessSpawner {
    public:
        virtual ~ProcessSpawner() = default;

        // Spawn `spec`'s program detached, appending its output to `log`. Returns
        // the new process's pid. Throws std::runtime_error if the launch fails.
        virtual std::int64_t spawn(const LaunchSpec &spec,
                                   const std::filesystem::path &log) = 0;

        // Request termination of a launched process. Best-effort: a pid that is
        // already gone is not an error.
        virtual void terminate(std::int64_t pid) = 0;
    };

    // The platform process spawner.
    std::unique_ptr<ProcessSpawner> make_process_spawner();
}
