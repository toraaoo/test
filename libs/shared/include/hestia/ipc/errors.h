#pragma once

// The protocol's error-code vocabulary in one place. The daemon raises these and
// the client matches on them; a named constant turns a typo from a silent runtime
// mismatch into a compile error. See A9 of the daemon refactor.
namespace hestia::ipc::errors {
    inline constexpr const char *kNotFound = "not_found";
    inline constexpr const char *kBadRequest = "bad_request";
    inline constexpr const char *kHandlerError = "handler_error";
    inline constexpr const char *kUnknownChannel = "unknown_channel";
    inline constexpr const char *kVersionMismatch = "version_mismatch";
}
