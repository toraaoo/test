#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <hestia/ipc/transport.h>

// The thin client SDK every frontend (CLI/TUI/desktop/tray) uses to drive the
// daemon. It is the single boundary frontends code against — they never link the
// engine. Each call round-trips one request to hestiad over the IPC bridge.
namespace hestia::client {
    struct AppInfo {
        std::string name;
        std::string version;
        std::string id;
        std::string vendor;
        std::string channel;
    };

    // A process to launch via the daemon. `kind` is "server" or "instance".
    struct ProcessSpec {
        std::string id;
        std::string kind = "server";
        std::string program;
        std::vector<std::string> args;
        std::string cwd;
    };

    // A supervised process as reported by the daemon.
    struct ProcessInfo {
        std::string id;
        std::string kind;
        std::string state;
        long long pid = 0;
        long long start_time = 0;
        std::string log_path;
    };

    class Client {
    public:
        // Connect to the running daemon. If none is running and `auto_spawn` is
        // true, start one and wait for it to come up. Throws std::runtime_error
        // if the daemon is unreachable (and could not be spawned).
        static Client connect(bool auto_spawn = true);

        // Typed channels. These throw std::runtime_error on a transport failure
        // or a daemon-side error (except config_get, which returns nullopt for a
        // missing key).
        std::optional<std::string> config_get(std::string_view key);
        void config_set(std::string_view key, std::string_view value);
        std::filesystem::path config_home();
        std::filesystem::path config_set_home(std::string_view dir);
        std::string greet(std::string_view name);
        AppInfo app_info();

        // Process supervision. start/stop/list/status/logs round-trip to the
        // daemon, which owns the processes so they outlive this client.
        ProcessInfo process_start(const ProcessSpec &spec);
        void process_stop(std::string_view id);
        std::vector<ProcessInfo> process_list();
        std::optional<ProcessInfo> process_status(std::string_view id);
        std::string process_logs(std::string_view id, int lines = 200);

    private:
        explicit Client(std::unique_ptr<ipc::Channel> channel)
            : channel_(std::move(channel)) {}

        std::unique_ptr<ipc::Channel> channel_;
    };
}
