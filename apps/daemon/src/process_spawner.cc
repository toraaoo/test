#include "process_spawner.h"

#include <stdexcept>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
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
        };
#endif
    }

    std::unique_ptr<ProcessSpawner> make_process_spawner() {
#if !defined(_WIN32)
        return std::make_unique<PosixProcessSpawner>();
#else
        throw std::runtime_error("process spawning is not yet implemented on Windows");
#endif
    }
}
