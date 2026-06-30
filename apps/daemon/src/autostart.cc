#include "autostart.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <hestia/app_info.h>

#if defined(__linux__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#if defined(_WIN32)
#include <windows.h>

#include "win_util.h"
#endif

// Autostart backends. Each registers the *running daemon's own* executable
// (resolved from the OS) so the registration survives the binary being moved.
namespace hestia::daemon {
    namespace fs = std::filesystem;

    namespace {
#if defined(__linux__) || defined(__APPLE__)
        // Run a command to completion with stdio silenced, returning its exit code
        // (or -1 if it could not be run). Used for best-effort hooks into the
        // platform service manager; autostart state is defined by the files on
        // disk, not by whether these helpers succeed.
        int run_command(const std::vector<std::string> &args) {
            std::vector<char *> argv;
            argv.reserve(args.size() + 1);
            for (const auto &arg: args) argv.push_back(const_cast<char *>(arg.c_str()));
            argv.push_back(nullptr);

            const pid_t pid = ::fork();
            if (pid < 0) return -1;
            if (pid == 0) {
                if (const int devnull = ::open("/dev/null", O_RDWR); devnull >= 0) {
                    ::dup2(devnull, 1);
                    ::dup2(devnull, 2);
                    if (devnull > 2) ::close(devnull);
                }
                ::execvp(argv[0], argv.data());
                _exit(127);
            }
            int status = 0;
            ::waitpid(pid, &status, 0);
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }
#endif

#if defined(__linux__)
        fs::path self_executable() {
            char buf[4096];
            const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
            if (n <= 0) throw std::runtime_error("cannot resolve daemon executable path");
            buf[n] = '\0';
            return fs::path(buf);
        }

        fs::path systemd_user_dir() {
            if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
                return fs::path(xdg) / "systemd" / "user";
            }
            if (const char *home = std::getenv("HOME"); home && *home) {
                return fs::path(home) / ".config" / "systemd" / "user";
            }
            throw std::runtime_error("cannot resolve systemd user directory: HOME is unset");
        }

        // systemd user unit. enable()/disable() manage the unit file and the
        // default.target.wants symlink natively — the exact effect of
        // `systemctl --user enable` — so it works even without a running user
        // manager (e.g. in a container). A best-effort daemon-reload lets a live
        // manager pick the change up without waiting for the next login.
        class SystemdAutostart final : public Autostart {
        public:
            void enable() override {
                const fs::path dir = systemd_user_dir();
                std::error_code ec;
                fs::create_directories(dir, ec);

                const fs::path unit = dir / kUnitName;
                std::ofstream f(unit, std::ios::trunc);
                if (!f) throw std::runtime_error("cannot write systemd unit: " + unit.string());
                f << unit_contents();
                f.close();

                const fs::path wants = dir / "default.target.wants";
                fs::create_directories(wants, ec);
                const fs::path link = wants / kUnitName;
                fs::remove(link, ec);
                fs::create_symlink(unit, link, ec);
                if (ec) throw std::runtime_error("cannot enable autostart: " + ec.message());

                run_command({"systemctl", "--user", "daemon-reload"});
            }

            void disable() override {
                const fs::path dir = systemd_user_dir();
                std::error_code ec;
                fs::remove(dir / "default.target.wants" / kUnitName, ec);
                fs::remove(dir / kUnitName, ec);
                run_command({"systemctl", "--user", "daemon-reload"});
            }

            bool is_enabled() const override {
                std::error_code ec;
                const fs::path link = systemd_user_dir() / "default.target.wants" / kUnitName;
                return fs::exists(link, ec);
            }

        private:
            static constexpr const char *kUnitName = "hestiad.service";

            static std::string unit_contents() {
                return std::string{"[Unit]\n"}
                       + "Description=" + APP_NAME + " launcher daemon\n"
                       + "After=default.target\n\n"
                       + "[Service]\n"
                       + "Type=simple\n"
                       + "ExecStart=" + self_executable().string() + " serve\n"
                       + "Restart=on-failure\n\n"
                       + "[Install]\n"
                       + "WantedBy=default.target\n";
            }
        };
#endif // __linux__

#if defined(__APPLE__)
        fs::path self_executable() {
            std::uint32_t size = 0;
            _NSGetExecutablePath(nullptr, &size);
            std::string buf(size, '\0');
            if (_NSGetExecutablePath(buf.data(), &size) != 0) {
                throw std::runtime_error("cannot resolve daemon executable path");
            }
            std::error_code ec;
            const fs::path resolved = fs::canonical(fs::path(buf.c_str()), ec);
            return ec ? fs::path(buf.c_str()) : resolved;
        }

        fs::path launch_agents_dir() {
            if (const char *home = std::getenv("HOME"); home && *home) {
                return fs::path(home) / "Library" / "LaunchAgents";
            }
            throw std::runtime_error("cannot resolve LaunchAgents directory: HOME is unset");
        }

        // macOS LaunchAgent. The plist is keyed by the reverse-DNS app id and runs
        // the daemon at login (RunAtLoad). enable()/disable() write/remove the
        // plist and best-effort load/unload it for the current session.
        class LaunchAgentAutostart final : public Autostart {
        public:
            void enable() override {
                const fs::path dir = launch_agents_dir();
                std::error_code ec;
                fs::create_directories(dir, ec);

                const fs::path plist = plist_path();
                std::ofstream f(plist, std::ios::trunc);
                if (!f) throw std::runtime_error("cannot write LaunchAgent: " + plist.string());
                f << plist_contents();
                f.close();

                run_command({"launchctl", "load", "-w", plist.string()});
            }

            void disable() override {
                const fs::path plist = plist_path();
                run_command({"launchctl", "unload", "-w", plist.string()});
                std::error_code ec;
                fs::remove(plist, ec);
            }

            bool is_enabled() const override {
                std::error_code ec;
                return fs::exists(plist_path(), ec);
            }

        private:
            static fs::path plist_path() {
                return launch_agents_dir() / (std::string{APP_ID} + ".plist");
            }

            static std::string plist_contents() {
                const std::string exec = self_executable().string();
                return std::string{"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"}
                       + "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                         "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                       + "<plist version=\"1.0\">\n"
                       + "<dict>\n"
                       + "  <key>Label</key>\n  <string>" + APP_ID + "</string>\n"
                       + "  <key>ProgramArguments</key>\n  <array>\n"
                       + "    <string>" + exec + "</string>\n"
                       + "    <string>serve</string>\n"
                       + "  </array>\n"
                       + "  <key>RunAtLoad</key>\n  <true/>\n"
                       + "</dict>\n"
                       + "</plist>\n";
            }
        };
#endif // __APPLE__

#if defined(_WIN32)
        fs::path self_executable() {
            wchar_t buf[MAX_PATH];
            const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
            if (n == 0 || n == MAX_PATH) {
                throw std::runtime_error("cannot resolve daemon executable path");
            }
            return fs::path(std::wstring(buf, n));
        }

        // Run schtasks.exe with the given command line, returning its exit code
        // (or -1 if it could not be launched). schtasks owns task persistence, so
        // unlike the POSIX backends the state lives in the Task Scheduler, queried
        // back via is_enabled().
        int run_schtasks(const std::wstring &command_line) {
            std::wstring mutable_cmd = command_line; // CreateProcessW may modify it
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            if (!::CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE,
                                  CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                return -1;
            }
            ::WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 1;
            ::GetExitCodeProcess(pi.hProcess, &code);
            ::CloseHandle(pi.hProcess);
            ::CloseHandle(pi.hThread);
            return static_cast<int>(code);
        }

        // Windows logon Scheduled Task. Survives in the user session (so GUI
        // client instances launched by the daemon land on the user's desktop) but
        // not logout.
        class ScheduledTaskAutostart final : public Autostart {
        public:
            void enable() override {
                // /TR carries the command as one schtasks token: the exe path is
                // quoted (it may contain spaces) and the inner quotes are escaped
                // for the command-line tokenizer.
                const std::wstring exec = self_executable().wstring();
                const std::wstring tr = L"\\\"" + exec + L"\\\" serve";
                const std::wstring cmd = L"schtasks /Create /F /SC ONLOGON /TN \""
                                         + task_name() + L"\" /TR \"" + tr + L"\"";
                if (run_schtasks(cmd) != 0) {
                    throw std::runtime_error("schtasks failed to create the autostart task");
                }
            }

            void disable() override {
                run_schtasks(L"schtasks /Delete /F /TN \"" + task_name() + L"\"");
            }

            bool is_enabled() const override {
                return run_schtasks(L"schtasks /Query /TN \"" + task_name() + L"\"") == 0;
            }

        private:
            static std::wstring task_name() {
                return widen(std::string{APP_NAME} + " Daemon");
            }
        };
#endif // _WIN32
    } // namespace

    std::unique_ptr<Autostart> make_autostart() {
#if defined(__linux__)
        return std::make_unique<SystemdAutostart>();
#elif defined(__APPLE__)
        return std::make_unique<LaunchAgentAutostart>();
#elif defined(_WIN32)
        return std::make_unique<ScheduledTaskAutostart>();
#else
        throw std::runtime_error("autostart is not supported on this platform");
#endif
    }
}
