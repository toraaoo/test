#include "services/services.h"

#include "handler_context.h"
#include "router.h"

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace hestia::daemon {
    namespace {
        int current_pid() {
#if !defined(_WIN32)
            return static_cast<int>(::getpid());
#else
            return 0;
#endif
        }
    }

    void register_health_service(Router &router) {
        router.on("health.ping", [](const ipc::Request &, HandlerContext &) {
            return ipc::Response::success({{"status", "alive"}, {"pid", current_pid()}});
        });
    }
}
