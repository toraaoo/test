#pragma once

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// The ProcessSupervisor seam (Phase 3). The daemon owns every launched process
// — game servers and game client instances — so they outlive the frontend that
// started them. All OS divergence (pidfd/subreaper on Linux, kqueue on macOS,
// Job Objects on Windows) lives behind this interface; the process table and
// lifecycle policy above it stay platform-neutral.
namespace hestia::daemon {
    // What kind of process this is — drives the default restart policy (servers
    // may auto-restart; client instances do not).
    enum class ProcessKind { Server, Instance };

    enum class ProcessState { Starting, Running, Exited, Crashed };

    // What to do when a supervised process exits unexpectedly.
    struct RestartPolicy {
        bool auto_restart = false;
        int max_retries = 0;
        std::chrono::milliseconds backoff{1000};
    };

    // A request to launch a process.
    struct LaunchSpec {
        std::string id; // caller-assigned, stable across restarts
        ProcessKind kind = ProcessKind::Server;
        std::filesystem::path program;
        std::vector<std::string> args;
        std::filesystem::path working_dir;
        RestartPolicy restart{};
    };

    // A row of the persisted process table. Serialized so a restarted (or
    // crashed-and-relaunched) daemon can re-adopt what is still running.
    struct ProcessRecord {
        std::string id;
        ProcessKind kind = ProcessKind::Server;
        std::int64_t pid = 0;
        std::int64_t start_time = 0; // disambiguates PID reuse on re-adoption
        std::filesystem::path log_path;
        ProcessState state = ProcessState::Starting;
    };

    class ProcessSupervisor {
    public:
        virtual ~ProcessSupervisor() = default;

        // Spawn detached from any UI, with stdout/stderr redirected to a log file
        // at the OS level (so logs survive even a daemon crash).
        virtual ProcessRecord start(const LaunchSpec &spec) = 0;

        // Stop a supervised process by id. No-op if it is not running.
        virtual void stop(const std::string &id) = 0;

        virtual std::vector<ProcessRecord> list() const = 0;
        virtual std::optional<ProcessRecord> status(const std::string &id) const = 0;

        // On daemon startup, reconcile the persisted table against what is
        // actually alive and re-adopt survivors (PID + start_time, or subreaper /
        // Job Object handle depending on platform).
        virtual void reconcile() = 0;
    };
}
