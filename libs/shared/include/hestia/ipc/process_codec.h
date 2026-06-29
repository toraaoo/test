#pragma once

#include <string_view>

#include <nlohmann/json.hpp>

#include <hestia/ipc/process.h>

// The single home for process domain types ⇄ JSON. The daemon's supervisor, the
// daemon's handlers, and the client SDK all (de)serialize through here, so a field
// added to a record is added once and the two sides cannot drift. See A4 of the
// daemon refactor.
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
