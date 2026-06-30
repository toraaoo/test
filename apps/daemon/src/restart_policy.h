#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>

#include "process_types.h"

// Pure restart-decision logic — no I/O, no clock of its own, no locks. The retry
// cap and the backoff window live here so they can be unit-tested in isolation.
namespace hestia::daemon::restart {
    using Clock = std::chrono::steady_clock;

    // Healthy uptime that refreshes the retry budget, making max_retries a
    // per-window cap rather than a lifetime one.
    inline constexpr std::chrono::seconds kStableUptime{60};

    // Ceiling on the escalating backoff so a crash loop settles at a slow retry.
    inline constexpr std::chrono::milliseconds kMaxBackoff{5 * 60 * 1000};

    // When a crash becomes eligible to retry again: now plus an exponential
    // backoff (base << restarts) capped at kMaxBackoff, so a crash-looping
    // process slows down instead of hammering at a fixed rate.
    inline Clock::time_point backoff_until(const ProcessRecord &rec, Clock::time_point now) {
        const int shift = std::min(rec.restarts, 20);
        auto delay = rec.restart.backoff * (std::int64_t{1} << shift);
        return now + std::min<std::chrono::milliseconds>(delay, kMaxBackoff);
    }

    inline bool should_reset_retries(const ProcessRecord &rec, Clock::duration uptime) {
        return rec.state == ProcessState::Running && rec.restarts > 0 &&
               uptime >= kStableUptime;
    }

    // Whether a record should be relaunched on this tick. Only servers auto-restart
    // (relaunching a game instance the user closed would be wrong); beyond that it
    // must be crashed, opt into auto-restart, have retries left, and have cleared
    // its backoff. `next_due` is the scheduled retry time (nullopt if none).
    inline bool should_restart(const ProcessRecord &rec, Clock::time_point now,
                               std::optional<Clock::time_point> next_due) {
        if (rec.kind != ProcessKind::Server) return false;
        if (rec.state != ProcessState::Crashed || !rec.restart.auto_restart) return false;
        if (rec.restart.max_retries > 0 && rec.restarts >= rec.restart.max_retries) return false;
        if (next_due && now < *next_due) return false; // still backing off
        return true;
    }
}
