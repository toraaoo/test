#pragma once

// Each daemon feature area registers its channels onto the router in its own
// translation unit. Adding one is a new file plus one call in run_daemon.
namespace hestia::daemon {
    class Router;

    void register_health_service(Router &router);
    void register_app_service(Router &router);
    void register_config_service(Router &router);
    void register_process_service(Router &router);
    void register_autostart_service(Router &router);
    void register_events_service(Router &router);
}
