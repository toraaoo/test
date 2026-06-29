#include "hestia/ipc/endpoint.h"
#include "hestia/ipc/protocol.h"
#include "hestia/ipc/transport.h"

#include "config_service.h"
#include "process_supervisor.h"
#include "router.h"

#include <hestia/app_info.h>
#include <hestia/greeting.h>

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <string_view>

#if !defined(_WIN32)
#include <unistd.h>
#endif

// hestiad — the Hestia daemon.
//
//   hestiad            run the daemon: bind the endpoint, serve until signalled
//   hestiad ping       connect to a running daemon, print its health response
//
// The daemon owns the engine (config, greeting, and the launcher logic to come);
// frontends reach it over the IPC bridge via the client SDK. Process supervision
// and autostart arrive in later phases. Logging is plain stderr for now; the
// structured logger is wired in during the hardening phase.

namespace {
    // The serving listener, so the signal handler can unblock serve(). Only
    // stop() (async-signal-safe) is ever called from the handler.
    std::atomic<hestia::ipc::Listener *> g_listener{nullptr};

    void handle_signal(int) {
        if (auto *l = g_listener.load()) l->stop();
    }

    int current_pid() {
#if !defined(_WIN32)
        return static_cast<int>(::getpid());
#else
        return 0;
#endif
    }

    using hestia::ipc::Request;
    using hestia::ipc::Response;

    // Wire up every channel the daemon serves onto `router`.
    void register_handlers(hestia::daemon::Router &router,
                           hestia::daemon::ConfigService &config,
                           hestia::daemon::ProcessSupervisor &supervisor) {
        router.on("health.ping", [](const Request &) {
            return Response::success({{"status", "alive"}, {"pid", current_pid()}});
        });

        router.on("app.info", [](const Request &) {
            return Response::success({
                {"name", APP_NAME},
                {"version", APP_VERSION},
                {"id", APP_ID},
                {"vendor", APP_VENDOR},
                {"channel", APP_CHANNEL},
            });
        });

        router.on("app.greet", [](const Request &req) {
            const auto name = req.payload.value("name", std::string{});
            return Response::success({{"message", hestia::greeting::greet(name)}});
        });

        router.on("config.get", [&config](const Request &req) {
            const auto key = req.payload.at("key").get<std::string>();
            if (const auto value = config.get(key)) {
                return Response::success({{"value", *value}});
            }
            return Response::failure("not_found", "key not found: " + key);
        });

        router.on("config.set", [&config](const Request &req) {
            config.set(req.payload.at("key").get<std::string>(),
                       req.payload.at("value").get<std::string>());
            return Response::success();
        });

        router.on("config.home", [&config](const Request &) {
            return Response::success({{"path", config.home().string()}});
        });

        router.on("config.set-home", [&config](const Request &req) {
            const auto dir = req.payload.value("dir", std::string{});
            return Response::success({{"path", config.set_home(dir).string()}});
        });

        using hestia::daemon::launch_spec_from_json;
        using hestia::daemon::to_json;

        router.on("process.start", [&supervisor](const Request &req) {
            const auto record = supervisor.start(launch_spec_from_json(req.payload));
            return Response::success(to_json(record));
        });

        router.on("process.stop", [&supervisor](const Request &req) {
            supervisor.stop(req.payload.at("id").get<std::string>());
            return Response::success();
        });

        router.on("process.list", [&supervisor](const Request &) {
            nlohmann::json processes = nlohmann::json::array();
            for (const auto &record : supervisor.list()) processes.push_back(to_json(record));
            return Response::success({{"processes", processes}});
        });

        router.on("process.status", [&supervisor](const Request &req) {
            const auto id = req.payload.at("id").get<std::string>();
            if (const auto record = supervisor.status(id)) {
                return Response::success(to_json(*record));
            }
            return Response::failure("not_found", "no such process: " + id);
        });

        router.on("process.logs", [&supervisor](const Request &req) {
            const auto id = req.payload.at("id").get<std::string>();
            const int lines = req.payload.value("lines", 200);
            if (const auto text = supervisor.tail_log(id, lines)) {
                return Response::success({{"text", *text}});
            }
            return Response::failure("not_found", "no such process: " + id);
        });
    }

    int run_daemon() {
        const auto endpoint = hestia::ipc::default_endpoint();
        std::unique_ptr<hestia::ipc::Listener> listener;
        try {
            listener = hestia::ipc::bind_listener(endpoint);
        } catch (const std::exception &e) {
            std::cerr << "hestiad: cannot start: " << e.what() << '\n';
            return 1;
        }

        hestia::daemon::ConfigService config;
        auto supervisor = hestia::daemon::make_process_supervisor(config.home());
        supervisor->reconcile(); // re-adopt processes that survived a previous daemon

        hestia::daemon::Router router;
        register_handlers(router, config, *supervisor);

        g_listener.store(listener.get());
        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);
#if !defined(_WIN32)
        std::signal(SIGPIPE, SIG_IGN); // a client vanishing mid-write must not kill us
#endif

        std::cerr << "hestiad: listening on " << endpoint.string() << '\n';
        listener->serve([&router](std::string_view raw) { return router.dispatch(raw); });
        g_listener.store(nullptr);
        std::cerr << "hestiad: stopped\n";
        return 0;
    }

    int run_ping() {
        const auto endpoint = hestia::ipc::default_endpoint();
        try {
            auto channel = hestia::ipc::connect(endpoint);
            hestia::ipc::Request req;
            req.channel = "health.ping";
            std::cout << channel->send(hestia::ipc::encode(req)) << '\n';
            return 0;
        } catch (const std::exception &e) {
            std::cerr << "hestiad ping: " << e.what() << '\n';
            return 1;
        }
    }
}

int main(int argc, char **argv) {
    const std::string_view mode = argc > 1 ? argv[1] : "serve";
    if (mode == "ping") return run_ping();
    if (mode == "serve") return run_daemon();
    std::cerr << "usage: hestiad [serve|ping]\n";
    return 2;
}
