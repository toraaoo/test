#include "process_supervisor.h"

#include "liveness_probe.h"
#include "log_streamer.h"
#include "process_spawner.h"
#include "process_table.h"
#include "restart_policy.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_set>

#include <hestia/ipc/topics.h>

#include <spdlog/spdlog.h>

namespace hestia::daemon {
    namespace fs = std::filesystem;

    namespace {
        constexpr std::uintmax_t kMaxLogBytes = 10u * 1024 * 1024;

        // Cap log growth across crash-looping relaunches by rotating to <log>.1.
        void rotate_log_if_large(const fs::path &log) {
            std::error_code ec;
            if (const auto size = fs::file_size(log, ec); ec || size < kMaxLogBytes) return;
            fs::path rotated = log;
            rotated += ".1";
            fs::remove(rotated, ec);
            fs::rename(log, rotated, ec);
        }

        // Coordinates the collaborators (table, spawner, liveness probe, log
        // streamer, restart policy) under one lock and one background loop.
        class ProcessSupervisorImpl final : public ProcessSupervisor {
        public:
            explicit ProcessSupervisorImpl(fs::path data_dir)
                : logs_dir_(data_dir / "logs"),
                  table_(data_dir / "processes.json"),
                  spawner_(make_process_spawner()),
                  probe_(make_liveness_probe()) {}

            ~ProcessSupervisorImpl() override {
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
                    auto &records = table_.entries();
                    if (auto it = records.find(spec.id);
                        it != records.end() && probe_->matches(it->second)) {
                        throw std::runtime_error("process already running: " + spec.id);
                    }

                    std::error_code ec;
                    fs::create_directories(logs_dir_, ec);
                    const fs::path log = logs_dir_ / (spec.id + ".log");
                    const std::int64_t pid = spawner_->spawn(spec, log);

                    rec.id = spec.id;
                    rec.kind = spec.kind;
                    rec.pid = pid;
                    rec.start_time = probe_->read_start_time(pid);
                    rec.log_path = log;
                    rec.state = ProcessState::Running;
                    rec.program = spec.program;
                    rec.args = spec.args;
                    rec.working_dir = spec.working_dir;
                    rec.restart = spec.restart;
                    rec.restarts = 0;
                    records[spec.id] = rec;
                    owned_.insert(spec.id); // our child: reap it for the exit code
                    streamer_.reset(spec.id, log); // stream only new output
                    next_restart_.erase(spec.id);
                    launched_at_[spec.id] = restart::Clock::now();
                    table_.save();
                }
                spdlog::info("started process '{}' (pid {})", rec.id, rec.pid);
                emit(state_event(rec));
                return rec;
            }

            void stop(const std::string &id) override {
                std::optional<ProcessRecord> snapshot;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    auto &records = table_.entries();
                    const auto it = records.find(id);
                    if (it == records.end()) return;
                    if (probe_->is_alive(it->second.pid)) {
                        spawner_->terminate(it->second.pid);
                    }
                    // An operator stop is terminal: Exited is never auto-restarted.
                    it->second.state = ProcessState::Exited;
                    next_restart_.erase(id);
                    launched_at_.erase(id);
                    table_.save();
                    snapshot = it->second;
                }
                spdlog::info("stopped process '{}'", id);
                if (snapshot) emit(state_event(*snapshot));
            }

            std::vector<ProcessRecord> list() override {
                std::lock_guard<std::mutex> lk(mu_);
                return table_.snapshot();
            }

            std::optional<ProcessRecord> status(const std::string &id) override {
                std::lock_guard<std::mutex> lk(mu_);
                return table_.get(id);
            }

            std::optional<std::string> tail_log(const std::string &id, int max_lines) override {
                fs::path log;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    const auto rec = table_.get(id);
                    if (!rec) return std::nullopt;
                    log = rec->log_path;
                }
                return LogStreamer::tail(log, max_lines);
            }

            void reconcile() override {
                std::lock_guard<std::mutex> lk(mu_);
                bool changed = false;
                const auto now = restart::Clock::now();
                for (auto &[id, rec]: table_.entries()) {
                    // Stream only output produced from now on; history is available
                    // via tail_log.
                    streamer_.reset(id, rec.log_path);
                    if ((rec.state == ProcessState::Running ||
                         rec.state == ProcessState::Starting) && !probe_->matches(rec)) {
                        rec.state = ProcessState::Crashed; // died while we were down
                        next_restart_[id] = now; // eligible on the first tick
                        spdlog::warn("process '{}' died while the daemon was down", id);
                        changed = true;
                    } else if (rec.state == ProcessState::Running) {
                        launched_at_[id] = now;
                    }
                }
                if (changed) table_.save();
            }

        private:
            static constexpr auto kPollInterval = std::chrono::milliseconds(250);

            void emit(const ipc::Event &event) {
                if (sink_) sink_(event);
            }

            ipc::Event state_event(const ProcessRecord &rec) const {
                return ipc::Event{ipc::topics::kProcessState, ipc::to_json(rec)};
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
                const auto now = restart::Clock::now();
                auto &records = table_.entries();

                for (auto &[id, rec]: records) {
                    const bool was_running = rec.state == ProcessState::Running ||
                                             rec.state == ProcessState::Starting;
                    if (owned_.count(id)) {
                        // Our child: reap it. This frees the zombie even when the
                        // record is already terminal (e.g. after an operator stop).
                        if (const auto exit = spawner_->reap(rec.pid)) {
                            owned_.erase(id);
                            if (was_running) {
                                if (exit->signaled || exit->code != 0) {
                                    rec.state = ProcessState::Crashed;
                                    next_restart_[id] = restart::backoff_until(rec, now);
                                    spdlog::warn("process '{}' crashed (pid {}, {} {})",
                                                 id, rec.pid,
                                                 exit->signaled ? "signal" : "exit",
                                                 exit->code);
                                } else {
                                    // Clean exit is terminal: never auto-restarted.
                                    rec.state = ProcessState::Exited;
                                    next_restart_.erase(id);
                                    spdlog::info("process '{}' exited (pid {})", id, rec.pid);
                                }
                                events.push_back(state_event(rec));
                                changed = true;
                            }
                        }
                    } else if (was_running && !probe_->matches(rec)) {
                        // Re-adopted process (not our child): no exit code available.
                        rec.state = ProcessState::Crashed;
                        next_restart_[id] = restart::backoff_until(rec, now);
                        spdlog::warn("process '{}' crashed (pid {})", id, rec.pid);
                        events.push_back(state_event(rec));
                        changed = true;
                    }
                    if (std::string chunk = streamer_.read_new(id, rec.log_path); !chunk.empty()) {
                        events.push_back(ipc::Event{
                            ipc::topics::kProcessLog,
                            {{"id", id}, {"text", std::move(chunk)}}
                        });
                    }
                    if (const auto at = launched_at_.find(id);
                        at != launched_at_.end() &&
                        restart::should_reset_retries(rec, now - at->second)) {
                        rec.restarts = 0;
                        changed = true;
                    }
                }

                for (auto &[id, rec]: records) {
                    std::optional<restart::Clock::time_point> due;
                    if (const auto it = next_restart_.find(id); it != next_restart_.end()) {
                        due = it->second;
                    }
                    if (!restart::should_restart(rec, now, due)) continue;
                    try {
                        relaunch_locked(rec);
                        spdlog::info("restarted process '{}' (attempt {}, pid {})",
                                     id, rec.restarts, rec.pid);
                        events.push_back(state_event(rec));
                        changed = true;
                    } catch (...) {
                        spdlog::error("failed to restart process '{}', backing off", id);
                        next_restart_[id] = restart::backoff_until(rec, now); // retry later
                    }
                }

                if (changed) table_.save();
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
                rotate_log_if_large(rec.log_path);
                const std::int64_t pid = spawner_->spawn(spec, rec.log_path);
                rec.pid = pid;
                rec.start_time = probe_->read_start_time(pid);
                rec.state = ProcessState::Running;
                rec.restarts += 1;
                owned_.insert(rec.id); // relaunched: our child again
                next_restart_.erase(rec.id);
                launched_at_[rec.id] = restart::Clock::now();
                // The log appends to the same file, so the streamer's offset carries
                // over and the relaunched process's output keeps streaming.
            }

            fs::path logs_dir_;

            mutable std::mutex mu_;
            ProcessTable table_;
            std::unique_ptr<ProcessSpawner> spawner_;
            std::unique_ptr<LivenessProbe> probe_;
            LogStreamer streamer_;
            std::map<std::string, restart::Clock::time_point> next_restart_;
            std::map<std::string, restart::Clock::time_point> launched_at_;
            // Ids we spawned in this daemon lifetime, so they are our children and
            // are reaped via the spawner (real exit codes). Processes re-adopted by
            // reconcile() are NOT here — they are not our children, so their
            // liveness is polled through the probe instead. Not persisted.
            std::unordered_set<std::string> owned_;

            EventSink sink_;
            std::thread worker_;
            std::atomic<bool> running_{false};
            std::mutex cv_mu_;
            std::condition_variable cv_;
        };
    } // namespace

    std::unique_ptr<ProcessSupervisor> make_process_supervisor(const fs::path &data_dir) {
        return std::make_unique<ProcessSupervisorImpl>(data_dir);
    }
}
