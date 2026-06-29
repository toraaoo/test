#pragma once

// The protocol's event-topic vocabulary in one place, shared by the daemon
// (which publishes) and the client (which matches). See A9 of the daemon refactor.
namespace hestia::ipc::topics {
    inline constexpr const char *kProcessState = "process.state";
    inline constexpr const char *kProcessLog = "process.log";
}
