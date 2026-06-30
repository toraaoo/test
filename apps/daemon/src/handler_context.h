#pragma once

#include <memory>

#include <hestia/ipc/transport.h>

// The collaborators a request handler may need, bundled into one object passed to
// every handler. The service references are long-lived (owned by run_daemon); the
// calling connection varies per request, which lets streaming channels (e.g.
// events.subscribe) be ordinary handlers instead of serve-loop special cases.
namespace hestia::daemon {
    class ConfigService;
    class ProcessSupervisor;
    class EventHub;

    struct HandlerContext {
        ConfigService &config;
        ProcessSupervisor &supervisor;
        EventHub &hub;
        std::shared_ptr<ipc::Connection> connection;
    };
}
