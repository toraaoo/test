#include "process_spawner.h"

#include <stdexcept>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

namespace hestia::daemon {
    namespace fs = std::filesystem;

    namespace {
#if !defined(_WIN32)
        class PosixProcessSpawner final : public ProcessSpawner {
        public:
            // Double-fork so the grandchild reparents to init: it outlives this
            // daemon (even a crash) and never becomes a zombie we must reap.
            std::int64_t spawn(const LaunchSpec &spec, const fs::path &log) override {
                // Build everything that touches the heap BEFORE forking: the daemon
                // is multithreaded, so the child may only use async-signal-safe
                // calls between fork and exec.
                std::vector<std::string> arg_storage;
                arg_storage.reserve(spec.args.size() + 1);
                arg_storage.push_back(spec.program.string());
                for (const auto &arg: spec.args) arg_storage.push_back(arg);
                std::vector<char *> argv;
                argv.reserve(arg_storage.size() + 1);
                for (auto &arg: arg_storage) argv.push_back(arg.data());
                argv.push_back(nullptr);
                const std::string log_path = log.string();
                const std::string workdir = spec.working_dir.string();

                int report[2];
                if (::pipe(report) != 0) throw std::runtime_error("pipe failed");

                const pid_t middle = ::fork();
                if (middle < 0) {
                    ::close(report[0]);
                    ::close(report[1]);
                    throw std::runtime_error("fork failed");
                }

                if (middle == 0) {
                    ::close(report[0]);
                    ::setsid();
                    const pid_t grandchild = ::fork();
                    if (grandchild < 0) _exit(127);
                    if (grandchild > 0) {
                        const std::int64_t value = grandchild;
                        const ssize_t w = ::write(report[1], &value, sizeof(value));
                        (void) w;
                        _exit(0);
                    }
                    // Grandchild: redirect IO and exec the target.
                    ::close(report[1]);
                    if (const int in = ::open("/dev/null", O_RDONLY); in >= 0) {
                        ::dup2(in, 0);
                        if (in > 2) ::close(in);
                    }
                    if (const int out = ::open(log_path.c_str(),
                                               O_CREAT | O_WRONLY | O_APPEND, 0644);
                        out >= 0) {
                        ::dup2(out, 1);
                        ::dup2(out, 2);
                        if (out > 2) ::close(out);
                    }
                    if (!workdir.empty()) {
                        if (::chdir(workdir.c_str()) != 0) {
                            /* exec below may still fail */
                        }
                    }
                    ::execvp(argv[0], argv.data());
                    _exit(127); // exec failed
                }

                // Parent: read the grandchild pid, then reap the middle child.
                ::close(report[1]);
                std::int64_t grandchild_pid = 0;
                const ssize_t r = ::read(report[0], &grandchild_pid, sizeof(grandchild_pid));
                (void) r;
                ::close(report[0]);
                int status = 0;
                ::waitpid(middle, &status, 0);
                if (grandchild_pid <= 0) {
                    throw std::runtime_error("failed to launch " + spec.program.string());
                }
                return grandchild_pid;
            }

            void terminate(std::int64_t pid) override {
                if (pid > 0) ::kill(static_cast<pid_t>(pid), SIGTERM);
            }
        };
#endif

#if defined(_WIN32)
        std::wstring widen(const std::string &s) {
            if (s.empty()) return {};
            const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                                static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(static_cast<std::size_t>(n), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                  w.data(), n);
            return w;
        }

        // Quote one argument per the CommandLineToArgvW rules so paths and
        // arguments with spaces or quotes round-trip to the child intact.
        void append_arg(std::wstring &cmd, const std::wstring &arg) {
            if (!cmd.empty()) cmd.push_back(L' ');
            if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) {
                cmd += arg;
                return;
            }
            cmd.push_back(L'"');
            for (auto it = arg.begin();; ++it) {
                unsigned slashes = 0;
                while (it != arg.end() && *it == L'\\') { ++it; ++slashes; }
                if (it == arg.end()) {
                    cmd.append(slashes * 2, L'\\');
                    break;
                }
                if (*it == L'"') {
                    cmd.append(slashes * 2 + 1, L'\\');
                } else {
                    cmd.append(slashes, L'\\');
                }
                cmd.push_back(*it);
            }
            cmd.push_back(L'"');
        }

        class WindowsProcessSpawner final : public ProcessSpawner {
        public:
            // Detached child with stdout/stderr appended to the log file and stdin
            // from NUL. Detaching keeps it alive across daemon restarts.
            std::int64_t spawn(const LaunchSpec &spec, const fs::path &log) override {
                std::wstring cmd;
                append_arg(cmd, spec.program.wstring());
                for (const auto &arg: spec.args) append_arg(cmd, widen(arg));

                SECURITY_ATTRIBUTES sa{};
                sa.nLength = sizeof(sa);
                sa.bInheritHandle = TRUE;

                HANDLE log_handle = ::CreateFileW(
                    log.wstring().c_str(), FILE_APPEND_DATA,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
                HANDLE nul_handle = ::CreateFileW(
                    L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

                STARTUPINFOW si{};
                si.cb = sizeof(si);
                si.dwFlags = STARTF_USESTDHANDLES;
                si.hStdInput = nul_handle;
                si.hStdOutput = log_handle != INVALID_HANDLE_VALUE ? log_handle : nul_handle;
                si.hStdError = si.hStdOutput;

                const std::wstring workdir = spec.working_dir.wstring();
                std::wstring mutable_cmd = cmd; // CreateProcessW may modify it
                PROCESS_INFORMATION pi{};
                const BOOL ok = ::CreateProcessW(
                    nullptr, mutable_cmd.data(), nullptr, nullptr, /*inherit=*/TRUE,
                    DETACHED_PROCESS, nullptr,
                    workdir.empty() ? nullptr : workdir.c_str(), &si, &pi);

                if (log_handle != INVALID_HANDLE_VALUE) ::CloseHandle(log_handle);
                if (nul_handle != INVALID_HANDLE_VALUE) ::CloseHandle(nul_handle);

                if (!ok) throw std::runtime_error("failed to launch " + spec.program.string());
                ::CloseHandle(pi.hThread);
                ::CloseHandle(pi.hProcess);
                return static_cast<std::int64_t>(pi.dwProcessId);
            }

            void terminate(std::int64_t pid) override {
                if (pid <= 0) return;
                if (HANDLE h = ::OpenProcess(PROCESS_TERMINATE, FALSE,
                                             static_cast<DWORD>(pid))) {
                    ::TerminateProcess(h, 1);
                    ::CloseHandle(h);
                }
            }
        };
#endif
    }

    std::unique_ptr<ProcessSpawner> make_process_spawner() {
#if !defined(_WIN32)
        return std::make_unique<PosixProcessSpawner>();
#else
        return std::make_unique<WindowsProcessSpawner>();
#endif
    }
}
