#include "core/app/tray_launcher.h"
#include "core/app/main_util.h"

#include <filesystem>
#include <string>

#include <spdlog/spdlog.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace desktop::app {

void SpawnTray() {
#if defined(_WIN32)
    const std::string program = GetExecutableDirectory() + "\\tray.exe";
#else
    const std::string program = GetExecutableDirectory() + "/tray";
#endif

    std::error_code ec;
    if (!std::filesystem::exists(program, ec)) {
        spdlog::warn("desktop: tray helper not found at {}", program);
        return;
    }

#if defined(_WIN32)
    std::string cmd = program;
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (::CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                         DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
    } else {
        spdlog::warn("desktop: failed to launch tray (error {})", ::GetLastError());
    }
#else
    const pid_t pid = ::fork();
    if (pid < 0) {
        spdlog::warn("desktop: fork failed launching tray");
        return;
    }
    if (pid == 0) {
        ::setsid();
        if (const int devnull = ::open("/dev/null", O_RDWR); devnull >= 0) {
            ::dup2(devnull, 0);
            ::dup2(devnull, 1);
            ::dup2(devnull, 2);
            if (devnull > 2) ::close(devnull);
        }
        ::execl(program.c_str(), "tray", static_cast<char *>(nullptr));
        _exit(127);
    }
#endif
}

}  // namespace desktop::app
