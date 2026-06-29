#include "single_instance.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <cstdlib>
#include <string>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace hestia::tray {

#if defined(_WIN32)
    SingleInstance::SingleInstance() {
        HANDLE h = ::CreateMutexW(nullptr, TRUE, L"Local\\HestiaTraySingleton");
        if (h && ::GetLastError() == ERROR_ALREADY_EXISTS) {
            ::CloseHandle(h);
            return;
        }
        handle_ = reinterpret_cast<std::intptr_t>(h);
        primary_ = h != nullptr;
    }

    SingleInstance::~SingleInstance() {
        if (handle_ != -1) ::CloseHandle(reinterpret_cast<HANDLE>(handle_));
    }
#else
    namespace {
        std::string lock_path() {
            if (const char *run = std::getenv("XDG_RUNTIME_DIR"); run && *run) {
                return std::string(run) + "/hestia-tray.lock";
            }
            return "/tmp/hestia-tray-" + std::to_string(::getuid()) + ".lock";
        }
    }

    SingleInstance::SingleInstance() {
        const std::string path = lock_path();
        const int fd = ::open(path.c_str(), O_CREAT | O_RDWR, 0600);
        if (fd < 0) {
            primary_ = true; // can't create a lock file; don't block startup
            return;
        }
        // flock auto-releases on exit/crash, so no stale lock survives.
        if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
            ::close(fd);
            return;
        }
        handle_ = fd;
        primary_ = true;
    }

    SingleInstance::~SingleInstance() {
        if (handle_ != -1) {
            ::flock(static_cast<int>(handle_), LOCK_UN);
            ::close(static_cast<int>(handle_));
        }
    }
#endif
}
