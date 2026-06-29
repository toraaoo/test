#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The process-supervision domain types. They live in hestia_shared so the daemon
// (which owns the processes) and the client SDK (which reports them) share one
// definition and one wire codec — see ipc/process_codec.h. The wire shape is
// defined exactly once, in the codec, not re-derived on either side.
namespace hestia::ipc {
    // What kind of process this is — drives the default restart policy (servers
    // may auto-restart; client instances do not).
    enum class ProcessKind { Server, Instance };

    enum class ProcessState { Starting, Running, Exited, Crashed };

    // What to do when a supervised process exits unexpectedly. `max_retries` of 0
    // means restart without limit; a positive value caps the attempts.
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

    // A row of the persisted process table. Serialized so a restarted daemon can
    // re-adopt what is still running (pid + start_time disambiguates PID reuse)
    // and relaunch what should auto-restart (the launch fields are persisted too).
    struct ProcessRecord {
        std::string id;
        ProcessKind kind = ProcessKind::Server;
        std::int64_t pid = 0;
        std::int64_t start_time = 0;
        std::filesystem::path log_path;
        ProcessState state = ProcessState::Starting;

        // Enough to relaunch the process after a crash or a daemon restart.
        std::filesystem::path program;
        std::vector<std::string> args;
        std::filesystem::path working_dir;
        RestartPolicy restart{};
        int restarts = 0;
    };
}
