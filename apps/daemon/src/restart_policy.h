#pragma once

#include <chrono>
#include <optional>

#include "process_types.h"

// Pure restart-decision logic — no I/O, no clock of its own, no locks. The retry
// cap and the backoff window live here so they can be unit-tested in isolation.
namespace hestia::daemon::restart {
    using Clock = std::chrono::steady_clock;

    // When a crash becomes eligible to retry again: now + the policy's backoff.
    inline Clock::time_point backoff_until(const ProcessRecord &rec, Clock::time_point now) {
        return now + rec.restart.backoff;
    }

    // Whether a record should be relaunched on this tick. True only when it is
    // crashed, its policy opts into auto-restart, it has retries left, and any
    // backoff window has elapsed. `next_due` is the record's scheduled retry time
    // (nullopt if none is pending).
    inline bool should_restart(const ProcessRecord &rec, Clock::time_point now,
                               std::optional<Clock::time_point> next_due) {
        if (rec.state != ProcessState::Crashed || !rec.restart.auto_restart) return false;
        if (rec.restart.max_retries > 0 && rec.restarts >= rec.restart.max_retries) return false;
        if (next_due && now < *next_due) return false; // still backing off
        return true;
    }
}
