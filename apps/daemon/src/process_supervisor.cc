#include "process_supervisor.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

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
                case ProcessState::Running: return "running";
                case ProcessState::Exited: return "exited";
                case ProcessState::Crashed: return "crashed";
            }
            return "unknown";
        }

        ProcessState parse_state(std::string_view s) {
            if (s == "running") return ProcessState::Running;
            if (s == "exited") return ProcessState::Exited;
            if (s == "crashed") return ProcessState::Crashed;
            return ProcessState::Starting;
        }

        RestartPolicy restart_from_json(const json &r) {
            RestartPolicy policy;
            policy.auto_restart = r.value("auto", false);
            policy.max_retries = r.value("max_retries", 0);
            policy.backoff = std::chrono::milliseconds(
                r.value("backoff_ms", static_cast<std::int64_t>(1000)));
            return policy;
        }

        ProcessRecord record_from_json(const json &j) {
            ProcessRecord r;
            r.id = j.at("id").get<std::string>();
            r.kind = parse_kind(j.value("kind", "server"));
            r.pid = j.value("pid", static_cast<std::int64_t>(0));
            r.start_time = j.value("start_time", static_cast<std::int64_t>(0));
            r.log_path = j.value("log_path", std::string{});
            r.state = parse_state(j.value("state", "starting"));
            r.program = j.value("program", std::string{});
            if (j.contains("args")) {
                for (const auto &arg: j.at("args")) r.args.push_back(arg.get<std::string>());
            }
            r.working_dir = j.value("cwd", std::string{});
            if (j.contains("restart")) r.restart = restart_from_json(j.at("restart"));
            r.restarts = j.value("restarts", 0);
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

        std::uint64_t current_file_size(const fs::path &path) {
            std::error_code ec;
            const auto size = fs::file_size(path, ec);
            return ec ? 0 : static_cast<std::uint64_t>(size);
        }

        class PosixProcessSupervisor final : public ProcessSupervisor {
        public:
            explicit PosixProcessSupervisor(fs::path data_dir)
                : data_dir_(std::move(data_dir)),
                  table_path_(data_dir_ / "processes.json"),
                  logs_dir_(data_dir_ / "logs") {
                load_table();
            }

            ~PosixProcessSupervisor() override {
                running_.store(false);
                cv_.notify_all();
                if (worker_.joinable()) worker_.join();
            }

            void set_event_sink(EventSink sink) override { sink_ = std::move(sink); }

            void start_supervision() override {
                if (running_.exchange(true)) return; // already running
                worker_ = std::thread([this] { supervise_loop(); });
            }

            ProcessRecord start(const LaunchSpec &spec) override {
                if (spec.id.empty()) throw std::runtime_error("process id is required");
                if (spec.program.empty()) throw std::runtime_error("process program is required");

                ProcessRecord rec;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    if (auto it = records_.find(spec.id);
                        it != records_.end() && alive_locked(it->second)) {
                        throw std::runtime_error("process already running: " + spec.id);
                    }

                    std::error_code ec;
                    fs::create_directories(logs_dir_, ec);
                    const fs::path log = logs_dir_ / (spec.id + ".log");
                    const std::int64_t pid = spawn_detached(spec, log);

                    rec.id = spec.id;
                    rec.kind = spec.kind;
                    rec.pid = pid;
                    rec.start_time = read_start_time(pid);
                    rec.log_path = log;
                    rec.state = ProcessState::Running;
                    rec.program = spec.program;
                    rec.args = spec.args;
                    rec.working_dir = spec.working_dir;
                    rec.restart = spec.restart;
                    rec.restarts = 0;
                    records_[spec.id] = rec;
                    log_offsets_[spec.id] = current_file_size(log); // stream only new output
                    next_restart_.erase(spec.id);
                    save_table_locked();
                }
                emit(state_event(rec));
                return rec;
            }

            void stop(const std::string &id) override {
                std::optional<ProcessRecord> snapshot;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    const auto it = records_.find(id);
                    if (it == records_.end()) return;
                    if (is_alive(it->second.pid)) {
                        ::kill(static_cast<pid_t>(it->second.pid), SIGTERM);
                    }
                    // An operator stop is terminal: Exited is never auto-restarted.
                    it->second.state = ProcessState::Exited;
                    next_restart_.erase(id);
                    save_table_locked();
                    snapshot = it->second;
                }
                if (snapshot) emit(state_event(*snapshot));
            }

            std::vector<ProcessRecord> list() override {
                std::lock_guard<std::mutex> lk(mu_);
                std::vector<ProcessRecord> out;
                out.reserve(records_.size());
                for (const auto &[id, rec]: records_) out.push_back(rec);
                return out;
            }

            std::optional<ProcessRecord> status(const std::string &id) override {
                std::lock_guard<std::mutex> lk(mu_);
                const auto it = records_.find(id);
                if (it == records_.end()) return std::nullopt;
                return it->second;
            }

            std::optional<std::string> tail_log(const std::string &id, int max_lines) override {
                fs::path log;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    const auto it = records_.find(id);
                    if (it == records_.end()) return std::nullopt;
                    log = it->second.log_path;
                }
                std::ifstream f(log);
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

            void reconcile() override {
                std::lock_guard<std::mutex> lk(mu_);
                bool changed = false;
                const auto now = std::chrono::steady_clock::now();
                for (auto &[id, rec]: records_) {
                    // Stream only output produced from now on; history is available
                    // via tail_log.
                    log_offsets_[id] = current_file_size(rec.log_path);
                    if ((rec.state == ProcessState::Running ||
                         rec.state == ProcessState::Starting) && !alive_locked(rec)) {
                        rec.state = ProcessState::Crashed; // died while we were down
                        next_restart_[id] = now; // eligible on the first tick
                        changed = true;
                    }
                }
                if (changed) save_table_locked();
            }

        private:
            static constexpr auto kPollInterval = std::chrono::milliseconds(250);

            void emit(const ipc::Event &event) {
                if (sink_) sink_(event);
            }

            ipc::Event state_event(const ProcessRecord &rec) const {
                return ipc::Event{"process.state", to_json(rec)};
            }

            // A process is still ours only if its pid is alive AND its start time
            // matches what we recorded (guards against PID reuse). Caller holds mu_.
            bool alive_locked(const ProcessRecord &rec) const {
                return is_alive(rec.pid) &&
                       (rec.start_time == 0 || read_start_time(rec.pid) == rec.start_time);
            }

            // The supervision loop: detect deaths, stream new log output, and
            // enforce restart policies. Events are collected under the lock and
            // emitted after releasing it, so a slow subscriber never stalls a tick.
            void supervise_loop() {
                while (running_.load()) {
                    std::vector<ipc::Event> events;
                    {
                        std::lock_guard<std::mutex> lk(mu_);
                        tick_locked(events);
                    }
                    for (const auto &event: events) emit(event);

                    std::unique_lock<std::mutex> lk(cv_mu_);
                    cv_.wait_for(lk, kPollInterval, [this] { return !running_.load(); });
                }
            }

            void tick_locked(std::vector<ipc::Event> &events) {
                bool changed = false;
                const auto now = std::chrono::steady_clock::now();

                for (auto &[id, rec]: records_) {
                    if ((rec.state == ProcessState::Running ||
                         rec.state == ProcessState::Starting) && !alive_locked(rec)) {
                        rec.state = ProcessState::Crashed;
                        next_restart_[id] = now + rec.restart.backoff;
                        events.push_back(state_event(rec));
                        changed = true;
                    }
                    read_new_log_locked(rec, events);
                }

                for (auto &[id, rec]: records_) {
                    if (rec.state != ProcessState::Crashed || !rec.restart.auto_restart) continue;
                    if (rec.restart.max_retries > 0 && rec.restarts >= rec.restart.max_retries) {
                        continue;
                    }
                    if (const auto due = next_restart_.find(id);
                        due != next_restart_.end() && now < due->second) {
                        continue; // still backing off
                    }
                    try {
                        relaunch_locked(rec);
                        events.push_back(state_event(rec));
                        changed = true;
                    } catch (...) {
                        next_restart_[id] = now + rec.restart.backoff; // retry later
                    }
                }

                if (changed) save_table_locked();
            }

            // Append any new bytes of a process's log file as a process.log event.
            void read_new_log_locked(ProcessRecord &rec, std::vector<ipc::Event> &events) {
                std::ifstream f(rec.log_path, std::ios::binary);
                if (!f) return;
                f.seekg(0, std::ios::end);
                const std::streamoff size = f.tellg();
                if (size < 0) return;
                auto &offset = log_offsets_[rec.id];
                if (static_cast<std::uint64_t>(size) < offset) offset = 0; // truncated/rotated
                if (static_cast<std::uint64_t>(size) == offset) return; // nothing new
                f.seekg(static_cast<std::streamoff>(offset));
                std::string chunk((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
                offset += chunk.size();
                if (!chunk.empty()) {
                    events.push_back(ipc::Event{
                        "process.log",
                        {{"id", rec.id}, {"text", chunk}}
                    });
                }
            }

            // Relaunch a crashed process in place, reusing its log file. Caller
            // holds mu_; throws if the spawn fails.
            void relaunch_locked(ProcessRecord &rec) {
                LaunchSpec spec;
                spec.id = rec.id;
                spec.kind = rec.kind;
                spec.program = rec.program;
                spec.args = rec.args;
                spec.working_dir = rec.working_dir;
                spec.restart = rec.restart;
                const std::int64_t pid = spawn_detached(spec, rec.log_path);
                rec.pid = pid;
                rec.start_time = read_start_time(pid);
                rec.state = ProcessState::Running;
                rec.restarts += 1;
                next_restart_.erase(rec.id);
                // The log appends to the same file, so log_offsets_ carries over and
                // the relaunched process's output keeps streaming.
            }

            // Double-fork so the grandchild reparents to init: it outlives this
            // daemon (even a crash) and never becomes a zombie we must reap.
            std::int64_t spawn_detached(const LaunchSpec &spec, const fs::path &log) {
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

            void save_table_locked() {
                std::error_code ec;
                fs::create_directories(data_dir_, ec);
                json j = json::array();
                for (const auto &[id, rec]: records_) j.push_back(to_json(rec));
                std::ofstream f(table_path_, std::ios::trunc);
                f << j.dump(2);
            }

            void load_table() {
                std::ifstream f(table_path_);
                if (!f) return;
                try {
                    json j;
                    f >> j;
                    for (const auto &entry: j) {
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

            mutable std::mutex mu_;
            std::map<std::string, ProcessRecord> records_;
            std::map<std::string, std::uint64_t> log_offsets_;
            std::map<std::string, std::chrono::steady_clock::time_point> next_restart_;

            EventSink sink_;
            std::thread worker_;
            std::atomic<bool> running_{false};
            std::mutex cv_mu_;
            std::condition_variable cv_;
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
            {"program", r.program.string()},
            {"args", r.args},
            {"cwd", r.working_dir.string()},
            {
                "restart", {
                    {"auto", r.restart.auto_restart},
                    {"max_retries", r.restart.max_retries},
                    {"backoff_ms", static_cast<std::int64_t>(r.restart.backoff.count())},
                }
            },
            {"restarts", r.restarts},
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
            for (const auto &arg: payload.at("args")) {
                spec.args.push_back(arg.get<std::string>());
            }
        }
        spec.working_dir = payload.value("cwd", std::string{});
        if (payload.contains("restart")) {
            const auto &r = payload.at("restart");
            spec.restart.auto_restart = r.value("auto", false);
            spec.restart.max_retries = r.value("max_retries", 0);
            if (r.contains("backoff_ms")) {
                spec.restart.backoff = std::chrono::milliseconds(
                    r.value("backoff_ms", static_cast<std::int64_t>(1000)));
            }
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
