#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <hestia/ipc/protocol.h>

#include "process_types.h"

// The ProcessSupervisor seam. The daemon owns every launched process — game
// servers and game client instances — so they outlive the frontend that started
// them. Processes are spawned detached (double-fork) with output redirected to a
// log file at the OS level, so they survive even a daemon crash and can be
// re-adopted on the next start. All OS divergence lives behind this interface.
//
// The process domain types (ProcessKind/State, RestartPolicy, LaunchSpec,
// ProcessRecord) and their JSON codec live in hestia_shared (ipc/process.h,
// ipc/process_codec.h) so the daemon and the client SDK share one definition.
namespace hestia::daemon {
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

    // Construct the platform supervisor, persisting its table under `data_dir`.
    std::unique_ptr<ProcessSupervisor> make_process_supervisor(
        const std::filesystem::path &data_dir);
}
