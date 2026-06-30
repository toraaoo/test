#include "hestia/ipc/endpoint.h"
#include "hestia/ipc/errors.h"
#include "hestia/ipc/protocol.h"
#include "hestia/ipc/transport.h"

#include "config_service.h"
#include "event_hub.h"
#include "handler_context.h"
#include "process_supervisor.h"
#include "router.h"
#include "services/services.h"

#include <hestia/app_info.h>
#include <hestia/client/client.h>
#include <hestia/logging.h>

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>

// hestiad — the Hestia daemon.
//
//   hestiad [serve]    run the daemon: bind the endpoint, serve until signalled
//   hestiad ping       connect to a running daemon, report its identity
//
// main() only does bootstrap, signal handling, and the serve loop — every channel
// lives in a service under src/services/, registered onto the router.
namespace {
    // The serving listener, so the signal handler can unblock serve(). Only
    // stop() (async-signal-safe) is ever called from the handler.
    std::atomic<hestia::ipc::Listener *> g_listener{nullptr};

    void handle_signal(int) {
        if (auto *l = g_listener.load()) l->stop();
    }

    // Serve one client connection: loop reading request frames, dispatch each
    // through the router with a per-request context, and write the correlated
    // response. The context carries the connection, so streaming channels
    // (events.subscribe) are ordinary handlers.
    void serve_connection(std::shared_ptr<hestia::ipc::Connection> conn,
                          const hestia::daemon::Router &router,
                          hestia::daemon::ConfigService &config,
                          hestia::daemon::ProcessSupervisor &supervisor,
                          hestia::daemon::EventHub &hub) {
        while (auto frame = conn->recv()) {
            hestia::ipc::Request req;
            try {
                req = hestia::ipc::decode_request(*frame);
            } catch (const std::exception &e) {
                spdlog::warn("dropping malformed frame: {}", e.what());
                conn->send(hestia::ipc::encode(
                    hestia::ipc::Response::failure(hestia::ipc::errors::kBadRequest, e.what())));
                continue;
            }
            hestia::daemon::HandlerContext ctx{config, supervisor, hub, conn};
            auto res = router.route(req, ctx);
            res.id = req.id;
            conn->send(hestia::ipc::encode(res));
        }
        hub.unsubscribe(conn.get());
    }

    int run_daemon() {
        const auto endpoint = hestia::ipc::default_endpoint();
        std::unique_ptr<hestia::ipc::Listener> listener;
        try {
            listener = hestia::ipc::bind_listener(endpoint);
        } catch (const std::exception &e) {
            spdlog::error("cannot start: {}", e.what());
            return 1;
        }

        hestia::daemon::ConfigService config;
        hestia::daemon::EventHub hub;

        auto supervisor = hestia::daemon::make_process_supervisor(config.home());
        supervisor->set_event_sink([&hub](const hestia::ipc::Event &e) { hub.publish(e); });
        supervisor->reconcile(); // re-adopt processes that survived a previous daemon
        supervisor->start_supervision(); // poll liveness, stream logs, enforce restarts

        hestia::daemon::Router router;
        hestia::daemon::register_health_service(router);
        hestia::daemon::register_app_service(router);
        hestia::daemon::register_config_service(router);
        hestia::daemon::register_process_service(router);
        hestia::daemon::register_autostart_service(router);
        hestia::daemon::register_events_service(router);

        g_listener.store(listener.get());
        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);
#if !defined(_WIN32)
        std::signal(SIGPIPE, SIG_IGN); // a client vanishing mid-write must not kill us
#endif

        spdlog::info("hestiad listening on {}", endpoint.string());
        listener->serve([&](std::shared_ptr<hestia::ipc::Connection> conn) {
            spdlog::debug("client connected");
            serve_connection(std::move(conn), router, config, *supervisor, hub);
            spdlog::debug("client disconnected");
        });
        g_listener.store(nullptr);
        spdlog::info("hestiad stopped");
        return 0;
    }

    // `hestiad ping` reuses the client SDK's transport rather than reimplementing
    // connect/encode/recv: connecting performs the version handshake, so a clean
    // round-trip proves the daemon is reachable and compatible.
    int run_ping() {
        try {
            auto client = hestia::client::Client::connect(/*auto_spawn=*/false);
            const auto info = client.app_info();
            std::cout << info.name << ' ' << info.version << " — alive\n";
            return 0;
        } catch (const std::exception &e) {
            std::cerr << "hestiad ping: " << e.what() << '\n';
            return 1;
        }
    }
}

int main(int argc, char **argv) {
    CLI::App app{"hestiad — the Hestia daemon"};
    app.set_version_flag("--version", std::string(APP_NAME) + " " + APP_VERSION);
    app.fallthrough(); // accept the global -v/-q flags after the subcommand too

    bool verbose = false;
    bool quiet = false;
    app.add_flag("-v,--verbose", verbose, "Verbose (debug) logging");
    app.add_flag("-q,--quiet", quiet, "Warnings and errors only");

    // The daemon already runs in the foreground; the client detaches it when it
    // auto-spawns. --foreground is accepted for clarity and forward-compatibility.
    bool foreground = false;
    auto *serve = app.add_subcommand("serve", "Run the daemon (default)");
    serve->add_flag("--foreground", foreground, "Run attached to this terminal");
    auto *ping = app.add_subcommand("ping", "Check that a running daemon is reachable");
    app.require_subcommand(0, 1);

    CLI11_PARSE(app, argc, argv);

    const auto level = verbose  ? hestia::LogLevel::debug
                       : quiet  ? hestia::LogLevel::warn
                                : hestia::LogLevel::info;

    // ping is a one-shot foreground tool — stderr only. The long-lived daemon
    // also logs to a rotated file, since the client detaches its stderr.
    if (ping->parsed()) {
        hestia::init_logging(level);
        return run_ping();
    }
    hestia::init_logging(level, hestia::ipc::runtime_dir() / "hestiad.log");
    return run_daemon();
}
