#pragma once

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <hestia/ipc/protocol.h>

// The ProcessSupervisor seam. The daemon owns every launched process — game
// servers and game client instances — so they outlive the frontend that started
// them. Processes are spawned detached (double-fork) with output redirected to a
// log file at the OS level, so they survive even a daemon crash and can be
// re-adopted on the next start. All OS divergence lives behind this interface.
namespace hestia::daemon {
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

    // Called with each event the supervisor emits (a state change, a log chunk).
    // Set once at startup, before supervision begins.
    using EventSink = std::function<void(const ipc::Event &)>;

    class ProcessSupervisor {
    public:
        virtual ~ProcessSupervisor() = default;

        // Where to deliver supervisor events. Must be set before start_supervision.
        virtual void set_event_sink(EventSink sink) = 0;

        // Begin the background loop: poll process liveness, stream new log output,
        // and enforce restart policies. Runs until the supervisor is destroyed.
        virtual void start_supervision() = 0;

        // Spawn detached from any UI, stdout/stderr redirected to a log file.
        virtual ProcessRecord start(const LaunchSpec &spec) = 0;

        // Signal a supervised process to stop by id. No-op if it is not running.
        virtual void stop(const std::string &id) = 0;

        // list()/status() refresh liveness before answering, so they are not const.
        virtual std::vector<ProcessRecord> list() = 0;
        virtual std::optional<ProcessRecord> status(const std::string &id) = 0;

        // The last `max_lines` lines of a process's log, or nullopt if unknown.
        virtual std::optional<std::string> tail_log(const std::string &id, int max_lines) = 0;

        // On daemon startup, reconcile the persisted table against what is
        // actually alive and re-adopt survivors.
        virtual void reconcile() = 0;
    };

    // Wire helpers shared by the daemon's handlers and the persisted table.
    nlohmann::json to_json(const ProcessRecord &record);
    ProcessKind parse_kind(std::string_view kind);
    LaunchSpec launch_spec_from_json(const nlohmann::json &payload);

    // Construct the platform supervisor, persisting its table under `data_dir`.
    std::unique_ptr<ProcessSupervisor> make_process_supervisor(
        const std::filesystem::path &data_dir);
}
