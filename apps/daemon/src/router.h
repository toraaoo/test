#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>

#include <hestia/ipc/protocol.h>

namespace hestia::daemon {
    // Maps a channel name to a handler and turns a raw request frame into a raw
    // response frame: decode, route, encode. Decode failures, unknown channels,
    // and handler exceptions all become protocol-level error responses, so the
    // transport only ever sees a well-formed frame.
    class Router {
    public:
        using Handler = std::function<ipc::Response(const ipc::Request &)>;

        void on(std::string channel, Handler handler);

        std::string dispatch(std::string_view raw_request) const;

    private:
        std::map<std::string, Handler> handlers_;
    };
}
