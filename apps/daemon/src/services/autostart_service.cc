#include "services/services.h"

#include "autostart.h"
#include "handler_context.h"
#include "router.h"

namespace hestia::daemon {
    void register_autostart_service(Router &router) {
        // Autostart is constructed per call so an unsupported platform fails the
        // one request rather than the whole daemon, and so the registration always
        // resolves the daemon's current executable path.
        router.on("autostart.enable", [](const ipc::Request &, HandlerContext &) {
            make_autostart()->enable();
            return ipc::Response::success({{"enabled", true}});
        });

        router.on("autostart.disable", [](const ipc::Request &, HandlerContext &) {
            make_autostart()->disable();
            return ipc::Response::success({{"enabled", false}});
        });

        router.on("autostart.status", [](const ipc::Request &, HandlerContext &) {
            return ipc::Response::success({{"enabled", make_autostart()->is_enabled()}});
        });
    }
}
