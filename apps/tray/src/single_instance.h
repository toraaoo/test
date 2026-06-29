#pragma once

#include <cstdint>

namespace hestia::tray {
    // Process-wide single-instance guard. Acquired on construction; primary() is
    // false when another tray is already running for this user/session. The lock
    // is held for the object's lifetime and released on destruction or exit.
    class SingleInstance {
    public:
        SingleInstance();
        ~SingleInstance();
        SingleInstance(const SingleInstance &) = delete;
        SingleInstance &operator=(const SingleInstance &) = delete;

        bool primary() const { return primary_; }

    private:
        bool primary_ = false;
        std::intptr_t handle_ = -1; // fd (POSIX) / HANDLE (Windows)
    };
}
