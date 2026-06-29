#include "hestia/client/client.h"

#include "hestia/ipc/endpoint.h"
#include "hestia/ipc/protocol.h"
#include "hestia/ipc/transport.h"

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace hestia::client {
    namespace fs = std::filesystem;
    using nlohmann::json;

    namespace {
        ProcessInfo process_from_json(const json &p) {
            return ProcessInfo{
                p.value("id", std::string{}),
                p.value("kind", std::string{}),
                p.value("state", std::string{}),
                p.value("pid", 0LL),
                p.value("start_time", 0LL),
                p.value("log_path", std::string{}),
            };
        }

        ProcessEvent to_process_event(const ipc::Event &event) {
            ProcessEvent out;
            out.topic = event.topic;
            out.id = event.payload.value("id", std::string{});
            if (event.topic == "process.log") {
                out.log = event.payload.value("text", std::string{});
            } else {
                out.process = process_from_json(event.payload);
            }
            return out;
        }

        // Throw on a daemon-side error; otherwise hand the response back.
        ipc::Response must(ipc::Response res) {
            if (!res.ok) {
                throw std::runtime_error(res.error ? res.error->code + ": " + res.error->message
                                                   : "daemon error");
            }
            return res;
        }

#if !defined(_WIN32)
        // The directory of the current executable, so we can find a sibling
        // `hestiad` to auto-spawn before falling back to PATH.
        fs::path self_dir() {
            char buf[4096];
            const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
            if (n <= 0) return {};
            buf[n] = '\0';
            return fs::path(buf).parent_path();
        }

        void spawn_daemon() {
            const fs::path sibling = self_dir() / "hestiad";
            std::error_code ec;
            const std::string program = fs::exists(sibling, ec) ? sibling.string() : "hestiad";

            const pid_t pid = ::fork();
            if (pid < 0) throw std::runtime_error("failed to fork to start hestiad");
            if (pid == 0) {
                ::setsid(); // detach from the frontend's session — the daemon outlives it
                if (const int devnull = ::open("/dev/null", O_RDWR); devnull >= 0) {
                    ::dup2(devnull, 0);
                    ::dup2(devnull, 1);
                    ::dup2(devnull, 2);
                    if (devnull > 2) ::close(devnull);
                }
                ::execlp(program.c_str(), "hestiad", "serve", static_cast<char *>(nullptr));
                _exit(127); // exec failed
            }
            // Parent: the daemon does not exit, so we never reap it; it reparents
            // to init. We just wait for its socket below.
        }
#else
        void spawn_daemon() {
            throw std::runtime_error("auto-spawning hestiad is not yet implemented on Windows");
        }
#endif

        std::shared_ptr<ipc::Connection> connect_with_retry(const fs::path &endpoint) {
            // Poll briefly for the freshly-spawned daemon's socket to appear.
            for (int attempt = 0; attempt < 60; ++attempt) {
                try {
                    return ipc::connect(endpoint);
                } catch (const std::exception &) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
            return nullptr;
        }
    } // namespace

    // Drives one persistent connection: a reader thread demuxes inbound frames,
    // fulfilling pending requests by id and delivering events to the callback.
    struct Client::Detail {
        explicit Detail(std::shared_ptr<ipc::Connection> connection)
            : conn(std::move(connection)) {
            reader = std::thread([this] { read_loop(); });
        }

        ~Detail() {
            if (conn) conn->close();
            if (reader.joinable()) reader.join();
        }

        void read_loop() {
            while (auto frame = conn->recv()) {
                json j;
                try {
                    j = json::parse(*frame);
                } catch (...) {
                    continue; // ignore a malformed frame rather than tear down
                }
                if (ipc::is_event(j)) {
                    EventCallback cb;
                    {
                        std::lock_guard<std::mutex> lk(mu);
                        cb = on_event;
                    }
                    if (cb) cb(to_process_event(ipc::decode_event(j)));
                    continue;
                }
                ipc::Response res = ipc::decode_response(j);
                const long long id = res.id.value_or(0);
                std::lock_guard<std::mutex> lk(mu);
                ready[id] = std::move(res);
                cv.notify_all();
            }
            // The connection closed: wake every waiter so they fail instead of
            // blocking forever.
            std::lock_guard<std::mutex> lk(mu);
            closed = true;
            cv.notify_all();
        }

        ipc::Response call(const std::string &channel, json payload) {
            long long id;
            {
                std::lock_guard<std::mutex> lk(mu);
                if (closed) throw std::runtime_error("daemon connection lost");
                id = next_id++;
            }
            ipc::Request req;
            req.channel = channel;
            req.payload = std::move(payload);
            req.id = id;
            if (!conn->send(ipc::encode(req))) {
                throw std::runtime_error("daemon connection lost");
            }

            std::unique_lock<std::mutex> lk(mu);
            cv.wait(lk, [&] { return closed || ready.count(id) > 0; });
            const auto it = ready.find(id);
            if (it == ready.end()) throw std::runtime_error("daemon closed the connection");
            ipc::Response res = std::move(it->second);
            ready.erase(it);
            return res;
        }

        void set_event_callback(EventCallback cb) {
            std::lock_guard<std::mutex> lk(mu);
            on_event = std::move(cb);
        }

        std::shared_ptr<ipc::Connection> conn;
        std::thread reader;
        std::mutex mu;
        std::condition_variable cv;
        long long next_id = 1;
        std::map<long long, ipc::Response> ready;
        EventCallback on_event;
        bool closed = false;
    };

    Client::Client(std::unique_ptr<Detail> detail) : d_(std::move(detail)) {}
    Client::Client(Client &&) noexcept = default;
    Client &Client::operator=(Client &&) noexcept = default;
    Client::~Client() = default;

    Client Client::connect(bool auto_spawn) {
        const auto endpoint = ipc::default_endpoint();
        std::shared_ptr<ipc::Connection> conn;
        try {
            conn = ipc::connect(endpoint);
        } catch (const std::exception &) {
            if (!auto_spawn) throw std::runtime_error("hestiad is not running");
            spawn_daemon();
            conn = connect_with_retry(endpoint);
            if (!conn) throw std::runtime_error("started hestiad but it did not become reachable");
        }
        return Client(std::make_unique<Detail>(std::move(conn)));
    }

    std::optional<std::string> Client::config_get(std::string_view key) {
        const auto res = d_->call("config.get", {{"key", std::string(key)}});
        if (res.ok) return res.payload.value("value", std::string{});
        if (res.error && res.error->code == "not_found") return std::nullopt;
        throw std::runtime_error(res.error ? res.error->code + ": " + res.error->message
                                           : "config.get failed");
    }

    void Client::config_set(std::string_view key, std::string_view value) {
        must(d_->call("config.set",
                      {{"key", std::string(key)}, {"value", std::string(value)}}));
    }

    fs::path Client::config_home() {
        const auto res = must(d_->call("config.home", json::object()));
        return fs::path(res.payload.at("path").get<std::string>());
    }

    fs::path Client::config_set_home(std::string_view dir) {
        json payload = json::object();
        if (!dir.empty()) payload["dir"] = std::string(dir);
        const auto res = must(d_->call("config.set-home", std::move(payload)));
        return fs::path(res.payload.at("path").get<std::string>());
    }

    std::string Client::greet(std::string_view name) {
        const auto res = must(d_->call("app.greet", {{"name", std::string(name)}}));
        return res.payload.value("message", std::string{});
    }

    AppInfo Client::app_info() {
        const auto res = must(d_->call("app.info", json::object()));
        const auto &p = res.payload;
        return AppInfo{
            p.value("name", std::string{}),
            p.value("version", std::string{}),
            p.value("id", std::string{}),
            p.value("vendor", std::string{}),
            p.value("channel", std::string{}),
        };
    }

    ProcessInfo Client::process_start(const ProcessSpec &spec) {
        json payload = {
            {"id", spec.id},
            {"kind", spec.kind},
            {"program", spec.program},
            {"args", spec.args},
            {"restart", {
                {"auto", spec.restart.auto_restart},
                {"max_retries", spec.restart.max_retries},
                {"backoff_ms", spec.restart.backoff_ms},
            }},
        };
        if (!spec.cwd.empty()) payload["cwd"] = spec.cwd;
        return process_from_json(must(d_->call("process.start", std::move(payload))).payload);
    }

    void Client::process_stop(std::string_view id) {
        must(d_->call("process.stop", {{"id", std::string(id)}}));
    }

    std::vector<ProcessInfo> Client::process_list() {
        const auto res = must(d_->call("process.list", json::object()));
        std::vector<ProcessInfo> out;
        for (const auto &entry : res.payload.value("processes", json::array())) {
            out.push_back(process_from_json(entry));
        }
        return out;
    }

    std::optional<ProcessInfo> Client::process_status(std::string_view id) {
        const auto res = d_->call("process.status", {{"id", std::string(id)}});
        if (res.ok) return process_from_json(res.payload);
        if (res.error && res.error->code == "not_found") return std::nullopt;
        throw std::runtime_error(res.error ? res.error->code + ": " + res.error->message
                                           : "process.status failed");
    }

    std::string Client::process_logs(std::string_view id, int lines) {
        const auto res = must(d_->call("process.logs",
                                       {{"id", std::string(id)}, {"lines", lines}}));
        return res.payload.value("text", std::string{});
    }

    void Client::subscribe(EventCallback cb, std::string id_filter) {
        d_->set_event_callback(std::move(cb));
        json payload = json::object();
        if (!id_filter.empty()) payload["id"] = id_filter;
        must(d_->call("events.subscribe", std::move(payload)));
    }
}
