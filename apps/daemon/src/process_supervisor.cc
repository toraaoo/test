#include "process_supervisor.h"

#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

#if !defined(_WIN32)
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace hestia::daemon {
    namespace fs = std::filesystem;
    using nlohmann::json;

    namespace {
        const char *kind_str(ProcessKind k) {
            return k == ProcessKind::Instance ? "instance" : "server";
        }

        const char *state_str(ProcessState s) {
            switch (s) {
                case ProcessState::Starting: return "starting";
                case ProcessState::Running:  return "running";
                case ProcessState::Exited:   return "exited";
                case ProcessState::Crashed:  return "crashed";
            }
            return "unknown";
        }

        ProcessState parse_state(std::string_view s) {
            if (s == "running") return ProcessState::Running;
            if (s == "exited") return ProcessState::Exited;
            if (s == "crashed") return ProcessState::Crashed;
            return ProcessState::Starting;
        }

        ProcessRecord record_from_json(const json &j) {
            ProcessRecord r;
            r.id = j.at("id").get<std::string>();
            r.kind = parse_kind(j.value("kind", "server"));
            r.pid = j.value("pid", static_cast<std::int64_t>(0));
            r.start_time = j.value("start_time", static_cast<std::int64_t>(0));
            r.log_path = j.value("log_path", std::string{});
            r.state = parse_state(j.value("state", "starting"));
            return r;
        }

#if defined(__linux__)
        // Field 22 of /proc/<pid>/stat is the process start time (clock ticks
        // since boot). Combined with the pid it survives PID reuse: a different
        // process that later reuses the pid will have a different start time.
        std::int64_t read_start_time(std::int64_t pid) {
            std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
            if (!f) return 0;
            std::string content((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
            // comm (field 2) is parenthesised and may contain spaces — start
            // parsing after the last ')'.
            const auto close = content.rfind(')');
            if (close == std::string::npos) return 0;
            std::istringstream rest(content.substr(close + 1));
            std::string token;
            // After comm, the next token is field 3 (state); start time is field
            // 22, i.e. the 20th token from here.
            for (int i = 0; i < 20; ++i) {
                if (!(rest >> token)) return 0;
            }
            try {
                return std::stoll(token);
            } catch (...) {
                return 0;
            }
        }
#else
        std::int64_t read_start_time(std::int64_t) { return 0; }
#endif

#if !defined(_WIN32)
        bool is_alive(std::int64_t pid) {
            if (pid <= 0) return false;
            if (::kill(static_cast<pid_t>(pid), 0) == 0) return true;
            return errno == EPERM; // exists but not ours to signal
        }

        class PosixProcessSupervisor final : public ProcessSupervisor {
        public:
            explicit PosixProcessSupervisor(fs::path data_dir)
                : data_dir_(std::move(data_dir)),
                  table_path_(data_dir_ / "processes.json"),
                  logs_dir_(data_dir_ / "logs") {
                load_table();
            }

            ProcessRecord start(const LaunchSpec &spec) override {
                if (spec.id.empty()) throw std::runtime_error("process id is required");
                if (spec.program.empty()) throw std::runtime_error("process program is required");
                if (auto it = records_.find(spec.id);
                    it != records_.end() && is_alive(it->second.pid)) {
                    throw std::runtime_error("process already running: " + spec.id);
                }

                std::error_code ec;
                fs::create_directories(logs_dir_, ec);
                const fs::path log = logs_dir_ / (spec.id + ".log");

                const std::int64_t pid = spawn_detached(spec, log);

                ProcessRecord rec;
                rec.id = spec.id;
                rec.kind = spec.kind;
                rec.pid = pid;
                rec.start_time = read_start_time(pid);
                rec.log_path = log;
                rec.state = ProcessState::Running;
                records_[spec.id] = rec;
                save_table();
                return rec;
            }

            void stop(const std::string &id) override {
                const auto it = records_.find(id);
                if (it == records_.end()) return;
                if (is_alive(it->second.pid)) {
                    ::kill(static_cast<pid_t>(it->second.pid), SIGTERM);
                }
                it->second.state = ProcessState::Exited;
                save_table();
            }

            std::vector<ProcessRecord> list() override {
                refresh();
                std::vector<ProcessRecord> out;
                out.reserve(records_.size());
                for (const auto &[id, rec] : records_) out.push_back(rec);
                return out;
            }

            std::optional<ProcessRecord> status(const std::string &id) override {
                refresh();
                const auto it = records_.find(id);
                if (it == records_.end()) return std::nullopt;
                return it->second;
            }

            std::optional<std::string> tail_log(const std::string &id, int max_lines) override {
                const auto it = records_.find(id);
                if (it == records_.end()) return std::nullopt;
                std::ifstream f(it->second.log_path);
                if (!f) return std::string{};
                std::vector<std::string> lines;
                std::string line;
                while (std::getline(f, line)) lines.push_back(line);
                const std::size_t keep = max_lines > 0 ? static_cast<std::size_t>(max_lines) : 0;
                const std::size_t begin = lines.size() > keep ? lines.size() - keep : 0;
                std::string out;
                for (std::size_t i = begin; i < lines.size(); ++i) {
                    out += lines[i];
                    out += '\n';
                }
                return out;
            }

            void reconcile() override { refresh(); }

        private:
            // Update each record's state from actual liveness. A process is still
            // ours only if its pid is alive AND its start time matches what we
            // recorded (guards against PID reuse after a daemon restart).
            void refresh() {
                bool changed = false;
                for (auto &[id, rec] : records_) {
                    if (rec.state == ProcessState::Exited || rec.state == ProcessState::Crashed) {
                        continue;
                    }
                    const bool alive = is_alive(rec.pid) &&
                                       (rec.start_time == 0 ||
                                        read_start_time(rec.pid) == rec.start_time);
                    if (!alive) {
                        rec.state = ProcessState::Exited;
                        changed = true;
                    }
                }
                if (changed) save_table();
            }

            // Double-fork so the grandchild reparents to init: it outlives this
            // daemon (even a crash) and never becomes a zombie we must reap.
            std::int64_t spawn_detached(const LaunchSpec &spec, const fs::path &log) {
                int report[2];
                if (::pipe(report) != 0) throw std::runtime_error("pipe failed");

                const pid_t middle = ::fork();
                if (middle < 0) throw std::runtime_error("fork failed");

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
                    if (const int out = ::open(log.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
                        out >= 0) {
                        ::dup2(out, 1);
                        ::dup2(out, 2);
                        if (out > 2) ::close(out);
                    }
                    if (!spec.working_dir.empty()) {
                        if (::chdir(spec.working_dir.c_str()) != 0) { /* exec below may still fail */ }
                    }
                    std::vector<char *> argv;
                    argv.push_back(const_cast<char *>(spec.program.c_str()));
                    for (const auto &arg : spec.args) argv.push_back(const_cast<char *>(arg.c_str()));
                    argv.push_back(nullptr);
                    ::execvp(spec.program.c_str(), argv.data());
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

            void save_table() {
                std::error_code ec;
                fs::create_directories(data_dir_, ec);
                json j = json::array();
                for (const auto &[id, rec] : records_) j.push_back(to_json(rec));
                std::ofstream f(table_path_, std::ios::trunc);
                f << j.dump(2);
            }

            void load_table() {
                std::ifstream f(table_path_);
                if (!f) return;
                try {
                    json j;
                    f >> j;
                    for (const auto &entry : j) {
                        auto rec = record_from_json(entry);
                        records_[rec.id] = rec;
                    }
                } catch (...) {
                    // A corrupt table is not fatal: start with an empty one.
                }
            }

            fs::path data_dir_;
            fs::path table_path_;
            fs::path logs_dir_;
            std::map<std::string, ProcessRecord> records_;
        };
#endif // !_WIN32
    } // namespace

    nlohmann::json to_json(const ProcessRecord &r) {
        return json{
            {"id", r.id},
            {"kind", kind_str(r.kind)},
            {"pid", r.pid},
            {"start_time", r.start_time},
            {"log_path", r.log_path.string()},
            {"state", state_str(r.state)},
        };
    }

    ProcessKind parse_kind(std::string_view kind) {
        return kind == "instance" ? ProcessKind::Instance : ProcessKind::Server;
    }

    LaunchSpec launch_spec_from_json(const json &payload) {
        LaunchSpec spec;
        spec.id = payload.at("id").get<std::string>();
        spec.kind = parse_kind(payload.value("kind", "server"));
        spec.program = payload.at("program").get<std::string>();
        if (payload.contains("args")) {
            for (const auto &arg : payload.at("args")) {
                spec.args.push_back(arg.get<std::string>());
            }
        }
        spec.working_dir = payload.value("cwd", std::string{});
        if (payload.contains("restart")) {
            const auto &r = payload.at("restart");
            spec.restart.auto_restart = r.value("auto", false);
            spec.restart.max_retries = r.value("max_retries", 0);
        }
        return spec;
    }

    std::unique_ptr<ProcessSupervisor> make_process_supervisor(const fs::path &data_dir) {
#if !defined(_WIN32)
        return std::make_unique<PosixProcessSupervisor>(data_dir);
#else
        (void) data_dir;
        throw std::runtime_error("process supervision is not yet implemented on Windows");
#endif
    }
}
