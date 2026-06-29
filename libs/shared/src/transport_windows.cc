#include "hestia/ipc/transport.h"

#if defined(_WIN32)

// Windows named-pipe transport. Lands in a later phase; this stub keeps the
// cross-platform target list honest and fails loudly rather than silently
// linking an empty implementation.
#include <stdexcept>

namespace hestia::ipc {
    std::unique_ptr<Listener> bind_listener(const std::filesystem::path &) {
        throw std::runtime_error("ipc: Windows named-pipe transport not yet implemented");
    }

    std::shared_ptr<Connection> connect(const std::filesystem::path &) {
        throw std::runtime_error("ipc: Windows named-pipe transport not yet implemented");
    }
}

#endif // _WIN32
