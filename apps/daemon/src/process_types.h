#pragma once

#include <hestia/ipc/process.h>
#include <hestia/ipc/process_codec.h>

// Re-exports the shared process domain types into the daemon namespace so
// daemon-internal code reads `ProcessRecord`, not `ipc::ProcessRecord`.
namespace hestia::daemon {
    using ipc::LaunchSpec;
    using ipc::ProcessKind;
    using ipc::ProcessRecord;
    using ipc::ProcessState;
    using ipc::RestartPolicy;
}
