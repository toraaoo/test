#include "router.h"

namespace hestia::daemon {
    void Router::on(std::string channel, Handler handler) {
        handlers_[std::move(channel)] = std::move(handler);
    }

    std::string Router::dispatch(std::string_view raw_request) const {
        ipc::Request request;
        try {
            request = ipc::decode_request(raw_request);
        } catch (const std::exception &e) {
            return ipc::encode(ipc::Response::failure("bad_request", e.what()));
        }

        const auto it = handlers_.find(request.channel);
        if (it == handlers_.end()) {
            auto response = ipc::Response::failure(
                "unknown_channel", "unknown channel: " + request.channel);
            response.id = request.id;
            return ipc::encode(response);
        }

        ipc::Response response;
        try {
            response = it->second(request);
        } catch (const std::exception &e) {
            // A handler that throws (e.g. a missing payload field) becomes a clean
            // error rather than taking down the daemon.
            response = ipc::Response::failure("handler_error", e.what());
        }
        response.id = request.id;
        return ipc::encode(response);
    }
}
