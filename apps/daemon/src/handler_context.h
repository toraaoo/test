#pragma once

#include <memory>

#include <hestia/ipc/transport.h>

// The collaborators a request handler may need, bundled into one object passed to
// every handler. The service references are long-lived (owned by run_daemon); the
// calling connection varies per request and is what lets streaming/stateful
// channels — e.g. events.subscribe — be ordinary handlers instead of special
// cases in the serve loop. Adding a collaborator is a field here, not a new
// parameter threaded through every registration. See P1/A2/A3 of the refactor.
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
