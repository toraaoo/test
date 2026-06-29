#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The thin client SDK every frontend (CLI/TUI/desktop/tray) uses to drive the
// daemon. It is the single boundary frontends code against — they never link the
// engine. The client holds one persistent, multiplexed connection: typed calls
// are correlated by id (and may be issued concurrently), and the daemon can push
// events (log lines, state changes) to a subscriber on the same connection.
namespace hestia::client {
    struct AppInfo {
        std::string name;
        std::string version;
        std::string id;
        std::string vendor;
        std::string channel;
    };

    // What the daemon should do when a process exits unexpectedly. `max_retries`
    // of 0 means restart without limit.
    struct RestartPolicy {
        bool auto_restart = false;
        int max_retries = 0;
        long long backoff_ms = 1000;
    };

    // A process to launch via the daemon. `kind` is "server" or "instance".
    struct ProcessSpec {
        std::string id;
        std::string kind = "server";
        std::string program;
        std::vector<std::string> args;
        std::string cwd;
        RestartPolicy restart;
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

    // An event pushed by the daemon to a subscriber. A "process.state" event
    // carries `process`; a "process.log" event carries `log` (a chunk of new
    // output). `id` is the process the event concerns.
    struct ProcessEvent {
        std::string topic;
        std::string id;
        std::optional<ProcessInfo> process;
        std::optional<std::string> log;
    };

    using EventCallback = std::function<void(const ProcessEvent &)>;

    class Client {
    public:
        // Connect to the running daemon. If none is running and `auto_spawn` is
        // true, start one and wait for it to come up. Throws std::runtime_error
        // if the daemon is unreachable (and could not be spawned).
        static Client connect(bool auto_spawn = true);

        Client(Client &&) noexcept;
        Client &operator=(Client &&) noexcept;
        ~Client();

        // Typed channels. These throw std::runtime_error on a transport failure
        // or a daemon-side error (except config_get, which returns nullopt for a
        // missing key).
        std::optional<std::string> config_get(std::string_view key);
        void config_set(std::string_view key, std::string_view value);
        std::filesystem::path config_home();
        std::filesystem::path config_set_home(std::string_view dir);
        std::string greet(std::string_view name);
        AppInfo app_info();

        // Autostart: register/unregister the daemon to start with the user
        // session, and query the current state. Backed by the platform's native
        // mechanism (systemd user unit / LaunchAgent / logon Scheduled Task).
        void autostart_enable();
        void autostart_disable();
        bool autostart_status();

        // Process supervision. start/stop/list/status/logs round-trip to the
        // daemon, which owns the processes so they outlive this client.
        ProcessInfo process_start(const ProcessSpec &spec);
        void process_stop(std::string_view id);
        std::vector<ProcessInfo> process_list();
        std::optional<ProcessInfo> process_status(std::string_view id);
        std::string process_logs(std::string_view id, int lines = 200);

        // Stream live process events. `cb` is invoked on the connection's reader
        // thread for each matching event; pass an `id_filter` to scope it to one
        // process, or empty for all. Call before issuing further requests; the
        // history up to now is available via process_logs.
        void subscribe(EventCallback cb, std::string id_filter = {});

    private:
        struct Detail;
        explicit Client(std::unique_ptr<Detail> detail);

        std::unique_ptr<Detail> d_;
    };
}
