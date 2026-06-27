#include <spdlog/spdlog.h>

#include <hestia/helper.h>

namespace hestia {
    void print(const std::string &message) {
        spdlog::info("{}", message);
    }
}
