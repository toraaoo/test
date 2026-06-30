#pragma once

#include <functional>
#include <map>
#include <string>

#include <hestia/ipc/protocol.h>

#include "handler_context.h"

namespace hestia::daemon {
    // Maps a channel name to a handler and routes a decoded request to it. An
    // unknown channel or a handler exception becomes a protocol-level error
    // response, so the caller always gets a well-formed Response back.
    class Router {
    public:
        using Handler = std::function<ipc::Response(const ipc::Request &, HandlerContext &)>;

        void on(std::string channel, Handler handler);

        ipc::Response route(const ipc::Request &request, HandlerContext &ctx) const;

    private:
        std::map<std::string, Handler> handlers_;
    };
}
