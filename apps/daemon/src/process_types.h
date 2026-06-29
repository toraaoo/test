#pragma once

#include <hestia/ipc/process.h>
#include <hestia/ipc/process_codec.h>

// The process domain types live in hestia_shared (so the daemon and the client
// SDK share one definition and one codec). This header re-exports them into the
// daemon namespace so daemon-internal code reads `ProcessRecord`, not
// `ipc::ProcessRecord`, and gives the collaborators a single include for them.
namespace hestia::daemon {
    using ipc::LaunchSpec;
    using ipc::ProcessKind;
    using ipc::ProcessRecord;
    using ipc::ProcessState;
    using ipc::RestartPolicy;
}
