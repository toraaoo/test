#pragma once

#include <string_view>

#include <nlohmann/json.hpp>

#include <hestia/ipc/process.h>

// The single home for process domain types ⇄ JSON. Daemon and client SDK both
// (de)serialize through here, so a new field is added once and they cannot drift.
namespace hestia::ipc {
    const char *to_string(ProcessKind kind);
    const char *to_string(ProcessState state);
    ProcessKind parse_kind(std::string_view kind);
    ProcessState parse_state(std::string_view state);

    nlohmann::json to_json(const RestartPolicy &policy);
    RestartPolicy restart_from_json(const nlohmann::json &j);

    nlohmann::json to_json(const ProcessRecord &record);
    ProcessRecord record_from_json(const nlohmann::json &j);

    nlohmann::json to_json(const LaunchSpec &spec);
    LaunchSpec launch_spec_from_json(const nlohmann::json &payload);
}
