#include "router.h"

namespace hestia::daemon {
    void Router::on(std::string channel, Handler handler) {
        handlers_[std::move(channel)] = std::move(handler);
    }

    ipc::Response Router::route(const ipc::Request &request) const {
        const auto it = handlers_.find(request.channel);
        if (it == handlers_.end()) {
            return ipc::Response::failure("unknown_channel",
                                          "unknown channel: " + request.channel);
        }
        try {
            return it->second(request);
        } catch (const std::exception &e) {
            // A handler that throws (e.g. a missing payload field) becomes a clean
            // error rather than taking down the daemon.
            return ipc::Response::failure("handler_error", e.what());
        }
    }
}
