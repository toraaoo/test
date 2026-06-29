#include "hestia/ipc/process_codec.h"

namespace hestia::ipc {
    using nlohmann::json;

    const char *to_string(ProcessKind kind) {
        return kind == ProcessKind::Instance ? "instance" : "server";
    }

    const char *to_string(ProcessState state) {
        switch (state) {
            case ProcessState::Starting: return "starting";
            case ProcessState::Running: return "running";
            case ProcessState::Exited: return "exited";
            case ProcessState::Crashed: return "crashed";
        }
        return "unknown";
    }

    ProcessKind parse_kind(std::string_view kind) {
        return kind == "instance" ? ProcessKind::Instance : ProcessKind::Server;
    }

    ProcessState parse_state(std::string_view state) {
        if (state == "running") return ProcessState::Running;
        if (state == "exited") return ProcessState::Exited;
        if (state == "crashed") return ProcessState::Crashed;
        return ProcessState::Starting;
    }

    json to_json(const RestartPolicy &p) {
        return json{
            {"auto", p.auto_restart},
            {"max_retries", p.max_retries},
            {"backoff_ms", static_cast<std::int64_t>(p.backoff.count())},
        };
    }

    RestartPolicy restart_from_json(const json &j) {
        RestartPolicy policy;
        policy.auto_restart = j.value("auto", false);
        policy.max_retries = j.value("max_retries", 0);
        policy.backoff = std::chrono::milliseconds(
            j.value("backoff_ms", static_cast<std::int64_t>(1000)));
        return policy;
    }

    json to_json(const ProcessRecord &r) {
        return json{
            {"id", r.id},
            {"kind", to_string(r.kind)},
            {"pid", r.pid},
            {"start_time", r.start_time},
            {"log_path", r.log_path.string()},
            {"state", to_string(r.state)},
            {"program", r.program.string()},
            {"args", r.args},
            {"cwd", r.working_dir.string()},
            {"restart", to_json(r.restart)},
            {"restarts", r.restarts},
        };
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

    json to_json(const LaunchSpec &s) {
        json j{
            {"id", s.id},
            {"kind", to_string(s.kind)},
            {"program", s.program.string()},
            {"args", s.args},
            {"restart", to_json(s.restart)},
        };
        if (!s.working_dir.empty()) j["cwd"] = s.working_dir.string();
        return j;
    }

    LaunchSpec launch_spec_from_json(const json &payload) {
        LaunchSpec spec;
        spec.id = payload.at("id").get<std::string>();
        spec.kind = parse_kind(payload.value("kind", "server"));
        spec.program = payload.at("program").get<std::string>();
        if (payload.contains("args")) {
            for (const auto &arg: payload.at("args")) spec.args.push_back(arg.get<std::string>());
        }
        spec.working_dir = payload.value("cwd", std::string{});
        if (payload.contains("restart")) spec.restart = restart_from_json(payload.at("restart"));
        return spec;
    }
}
