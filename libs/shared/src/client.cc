#include "hestia/client/client.h"

#include "hestia/ipc/endpoint.h"
#include "hestia/ipc/protocol.h"

#include <chrono>
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
        // Send one request, decode the response, and throw on a daemon-side error.
        ipc::Response call(ipc::Channel &ch, const std::string &channel, json payload) {
            ipc::Request req;
            req.channel = channel;
            req.payload = std::move(payload);
            const ipc::Response res = ipc::decode_response(ch.send(ipc::encode(req)));
            if (!res.ok) {
                const std::string code = res.error ? res.error->code : "error";
                const std::string msg = res.error ? res.error->message : "daemon error";
                throw std::runtime_error(code + ": " + msg);
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

        std::unique_ptr<ipc::Channel> connect_with_retry(const fs::path &endpoint) {
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

    Client Client::connect(bool auto_spawn) {
        const auto endpoint = ipc::default_endpoint();
        try {
            return Client(ipc::connect(endpoint));
        } catch (const std::exception &) {
            if (!auto_spawn) throw std::runtime_error("hestiad is not running");
        }

        spawn_daemon();
        if (auto channel = connect_with_retry(endpoint)) {
            return Client(std::move(channel));
        }
        throw std::runtime_error("started hestiad but it did not become reachable");
    }

    std::optional<std::string> Client::config_get(std::string_view key) {
        ipc::Request req;
        req.channel = "config.get";
        req.payload = {{"key", std::string(key)}};
        const ipc::Response res = ipc::decode_response(channel_->send(ipc::encode(req)));
        if (res.ok) return res.payload.value("value", std::string{});
        if (res.error && res.error->code == "not_found") return std::nullopt;
        throw std::runtime_error(res.error ? res.error->code + ": " + res.error->message
                                           : "config.get failed");
    }

    void Client::config_set(std::string_view key, std::string_view value) {
        call(*channel_, "config.set",
             {{"key", std::string(key)}, {"value", std::string(value)}});
    }

    fs::path Client::config_home() {
        const auto res = call(*channel_, "config.home", json::object());
        return fs::path(res.payload.at("path").get<std::string>());
    }

    fs::path Client::config_set_home(std::string_view dir) {
        json payload = json::object();
        if (!dir.empty()) payload["dir"] = std::string(dir);
        const auto res = call(*channel_, "config.set-home", std::move(payload));
        return fs::path(res.payload.at("path").get<std::string>());
    }

    std::string Client::greet(std::string_view name) {
        const auto res = call(*channel_, "app.greet", {{"name", std::string(name)}});
        return res.payload.value("message", std::string{});
    }

    AppInfo Client::app_info() {
        const auto res = call(*channel_, "app.info", json::object());
        const auto &p = res.payload;
        return AppInfo{
            p.value("name", std::string{}),
            p.value("version", std::string{}),
            p.value("id", std::string{}),
            p.value("vendor", std::string{}),
            p.value("channel", std::string{}),
        };
    }

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
    } // namespace

    ProcessInfo Client::process_start(const ProcessSpec &spec) {
        json payload = {
            {"id", spec.id},
            {"kind", spec.kind},
            {"program", spec.program},
            {"args", spec.args},
        };
        if (!spec.cwd.empty()) payload["cwd"] = spec.cwd;
        return process_from_json(call(*channel_, "process.start", std::move(payload)).payload);
    }

    void Client::process_stop(std::string_view id) {
        call(*channel_, "process.stop", {{"id", std::string(id)}});
    }

    std::vector<ProcessInfo> Client::process_list() {
        const auto res = call(*channel_, "process.list", json::object());
        std::vector<ProcessInfo> out;
        for (const auto &entry : res.payload.value("processes", json::array())) {
            out.push_back(process_from_json(entry));
        }
        return out;
    }

    std::optional<ProcessInfo> Client::process_status(std::string_view id) {
        ipc::Request req;
        req.channel = "process.status";
        req.payload = {{"id", std::string(id)}};
        const ipc::Response res = ipc::decode_response(channel_->send(ipc::encode(req)));
        if (res.ok) return process_from_json(res.payload);
        if (res.error && res.error->code == "not_found") return std::nullopt;
        throw std::runtime_error(res.error ? res.error->code + ": " + res.error->message
                                           : "process.status failed");
    }

    std::string Client::process_logs(std::string_view id, int lines) {
        const auto res = call(*channel_, "process.logs",
                              {{"id", std::string(id)}, {"lines", lines}});
        return res.payload.value("text", std::string{});
    }
}
