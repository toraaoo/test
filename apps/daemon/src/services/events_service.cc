#include "services/services.h"

#include "event_hub.h"
#include "handler_context.h"
#include "router.h"

#include <optional>
#include <string>

namespace hestia::daemon {
    void register_events_service(Router &router) {
        // A streaming channel: it needs the calling connection to push to, which it
        // gets from the context — so it is an ordinary handler, not a special case
        // in the serve loop (A2). Closing the connection prunes the subscription.
        router.on("events.subscribe", [](const ipc::Request &req, HandlerContext &ctx) {
            std::optional<std::string> filter;
            if (req.payload.contains("id") && req.payload["id"].is_string()) {
                filter = req.payload["id"].get<std::string>();
            }
            ctx.hub.subscribe(ctx.connection, std::move(filter));
            return ipc::Response::success({{"subscribed", true}});
        });
    }
}
