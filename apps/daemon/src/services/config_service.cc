#include "services/services.h"

#include "config_service.h"
#include "handler_context.h"
#include "router.h"

#include <hestia/ipc/errors.h>

#include <string>

namespace hestia::daemon {
    void register_config_service(Router &router) {
        router.on("config.get", [](const ipc::Request &req, HandlerContext &ctx) {
            const auto key = req.payload.at("key").get<std::string>();
            if (const auto value = ctx.config.get(key)) {
                return ipc::Response::success({{"value", *value}});
            }
            return ipc::Response::failure(ipc::errors::kNotFound, "key not found: " + key);
        });

        router.on("config.set", [](const ipc::Request &req, HandlerContext &ctx) {
            ctx.config.set(req.payload.at("key").get<std::string>(),
                           req.payload.at("value").get<std::string>());
            return ipc::Response::success();
        });

        router.on("config.home", [](const ipc::Request &, HandlerContext &ctx) {
            return ipc::Response::success({{"path", ctx.config.home().string()}});
        });

        router.on("config.set-home", [](const ipc::Request &req, HandlerContext &ctx) {
            const auto dir = req.payload.value("dir", std::string{});
            return ipc::Response::success({{"path", ctx.config.set_home(dir).string()}});
        });
    }
}
