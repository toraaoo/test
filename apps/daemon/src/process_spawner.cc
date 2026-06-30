#include "process_spawner.h"

#include "win_util.h"

#include <stdexcept>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <mutex>
#include <unordered_map>
#include <windows.h>
#endif

namespace hestia::daemon {
    namespace fs = std::filesystem;

    namespace {
#if !defined(_WIN32)
        class PosixProcessSpawner final : public ProcessSpawner {
        public:
            // Single fork: the child is a direct child of the daemon, so we learn
            // its exit by reap()ing it. setsid() puts it in its own process group
            // (pgid == pid) so terminate() takes down the whole tree, and it still
            // outlives the daemon — if we die it simply reparents to init.
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

                const pid_t child = ::fork();
                if (child < 0) throw std::runtime_error("fork failed");

                if (child == 0) {
                    ::setsid(); // own process group: pgid == pid
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
                    // exec failed: surface it as exit 127, which the supervisor
                    // reaps and treats as a crash.
                    _exit(127);
                }

                return child;
            }

            std::optional<ProcessExit> reap(std::int64_t pid) override {
                if (pid <= 0) return std::nullopt;
                int status = 0;
                const pid_t r = ::waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
                if (r != static_cast<pid_t>(pid)) {
                    // 0: still running. -1/ECHILD: not our child (re-adopted).
                    return std::nullopt;
                }
                if (WIFSIGNALED(status)) {
                    return ProcessExit{WTERMSIG(status), true};
                }
                return ProcessExit{WIFEXITED(status) ? WEXITSTATUS(status) : -1, false};
            }

            void terminate(std::int64_t pid) override { signal_group(pid, SIGTERM); }

            void kill(std::int64_t pid) override { signal_group(pid, SIGKILL); }

        private:
            // pid is also the pgid: signal the whole group, falling back to the
            // lone pid if the group is already gone.
            static void signal_group(std::int64_t pid, int sig) {
                if (pid <= 0) return;
                if (::kill(static_cast<pid_t>(-pid), sig) != 0) {
                    ::kill(static_cast<pid_t>(pid), sig);
                }
            }
        };
#endif

#if defined(_WIN32)
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
            ~WindowsProcessSpawner() override {
                // No kill-on-close, so dropping the handles leaves the processes
                // running — they outlive the daemon.
                std::lock_guard<std::mutex> lk(mu_);
                for (auto &[pid, owned]: procs_) {
                    if (owned.job) ::CloseHandle(owned.job);
                    ::CloseHandle(owned.process);
                }
            }

            // Child with stdout/stderr appended to the log file and stdin from NUL.
            // Detaching the console keeps it alive across daemon restarts; the Job
            // Object lets terminate() take down the whole tree, not just the pid. We
            // keep the process handle so reap() reads its exit reliably and the OS
            // can't recycle the pid while we still reference it.
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

                // Anonymous job, no kill-on-close (see the destructor). Start the
                // process suspended so it joins the job before it runs any code.
                HANDLE job = ::CreateJobObjectW(nullptr, nullptr);

                const std::wstring workdir = spec.working_dir.wstring();
                std::wstring mutable_cmd = cmd; // CreateProcessW may modify it
                PROCESS_INFORMATION pi{};
                const BOOL ok = ::CreateProcessW(
                    nullptr, mutable_cmd.data(), nullptr, nullptr, /*inherit=*/TRUE,
                    DETACHED_PROCESS | CREATE_SUSPENDED, nullptr,
                    workdir.empty() ? nullptr : workdir.c_str(), &si, &pi);

                if (log_handle != INVALID_HANDLE_VALUE) ::CloseHandle(log_handle);
                if (nul_handle != INVALID_HANDLE_VALUE) ::CloseHandle(nul_handle);

                if (!ok) {
                    if (job) ::CloseHandle(job);
                    throw std::runtime_error("failed to launch " + spec.program.string());
                }

                if (job && !::AssignProcessToJobObject(job, pi.hProcess)) {
                    ::CloseHandle(job); // grouping unavailable; run ungrouped
                    job = nullptr;
                }
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    procs_[pi.dwProcessId] = Owned{job, pi.hProcess};
                }

                ::ResumeThread(pi.hThread);
                ::CloseHandle(pi.hThread);
                return static_cast<std::int64_t>(pi.dwProcessId);
            }

            std::optional<ProcessExit> reap(std::int64_t pid) override {
                if (pid <= 0) return std::nullopt;
                Owned owned;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    const auto it = procs_.find(static_cast<DWORD>(pid));
                    if (it == procs_.end()) return std::nullopt; // not our child
                    owned = it->second;
                    if (::WaitForSingleObject(owned.process, 0) != WAIT_OBJECT_0) {
                        return std::nullopt; // still running
                    }
                    procs_.erase(it);
                }
                DWORD code = 0;
                ::GetExitCodeProcess(owned.process, &code);
                if (owned.job) ::CloseHandle(owned.job);
                ::CloseHandle(owned.process);
                return ProcessExit{static_cast<int>(code), false};
            }

            void terminate(std::int64_t pid) override {
                if (pid <= 0) return;
                Owned owned{};
                bool found = false;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    if (const auto it = procs_.find(static_cast<DWORD>(pid));
                        it != procs_.end()) {
                        owned = it->second;
                        found = true;
                        procs_.erase(it);
                    }
                }
                if (found) {
                    if (owned.job) {
                        ::TerminateJobObject(owned.job, 1);
                        ::CloseHandle(owned.job);
                    } else {
                        ::TerminateProcess(owned.process, 1);
                    }
                    ::CloseHandle(owned.process);
                    return;
                }
                // Not ours (re-adopted after a restart): kill the pid by handle.
                if (HANDLE h = ::OpenProcess(PROCESS_TERMINATE, FALSE,
                                             static_cast<DWORD>(pid))) {
                    ::TerminateProcess(h, 1);
                    ::CloseHandle(h);
                }
            }

            void kill(std::int64_t pid) override { terminate(pid); }

        private:
            struct Owned {
                HANDLE job = nullptr;     // nullptr when grouping was unavailable
                HANDLE process = nullptr; // kept open until reaped / terminated
            };

            std::mutex mu_;
            std::unordered_map<DWORD, Owned> procs_;
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
