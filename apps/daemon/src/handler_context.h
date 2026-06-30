#pragma once

#include <memory>

#include <hestia/ipc/transport.h>

// The collaborators a request handler may need, bundled into one object passed to
// every handler. The engine and daemon-runtime references are long-lived (owned
// by run_daemon); the calling connection varies per request, which lets streaming
// channels (e.g. events.subscribe) be ordinary handlers instead of serve-loop
// special cases. Domain logic lives behind `engine`; the supervisor and hub are
// daemon-runtime infrastructure.
namespace hestia::engine {
    class Engine;
}

namespace hestia::daemon {
    class ProcessSupervisor;
    class EventHub;

    struct HandlerContext {
        engine::Engine &engine;
        ProcessSupervisor &supervisor;
        EventHub &hub;
        std::shared_ptr<ipc::Connection> connection;
        ipc::Peer peer;
    };
}
