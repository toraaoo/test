#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

// The daemon protocol envelope, layered on top of the raw frame transport. Both
// sides — the daemon's router and the client SDK — encode/decode through here, so
// the wire format lives in exactly one place. See docs/daemon-protocol.md.
namespace hestia::ipc {
    // The protocol major version carried by every envelope. Client and daemon
    // refuse an incompatible major at connect rather than guessing (see the
    // "Versioning" section of docs/daemon-protocol.md). Bump on a breaking wire
    // change; additive fields do not need a bump.
    inline constexpr int kProtocolVersion = 1;

    // Whether a peer advertising major `version` can talk to us. Same major only;
    // additive changes within a major stay compatible.
    inline constexpr bool compatible(int version) { return version == kProtocolVersion; }

    // A request: a channel name, a JSON payload, and an optional correlation id.
    struct Request {
        std::string channel;
        nlohmann::json payload = nlohmann::json::object();
        std::optional<long long> id;
        int version = kProtocolVersion;
    };

    struct Error {
        std::string code;
        std::string message;
    };

    // A response: success carries a payload; failure carries an error. The id
    // echoes the request's id when present.
    struct Response {
        bool ok = false;
        nlohmann::json payload = nlohmann::json::object();
        std::optional<Error> error;
        std::optional<long long> id;
        int version = kProtocolVersion;

        static Response success(nlohmann::json payload = nlohmann::json::object());
        static Response failure(std::string code, std::string message);
    };

    // An unsolicited push from the daemon to a subscribed client (a log line, a
    // process state change). It carries no id — it is not a reply to any request.
    struct Event {
        std::string topic; // e.g. "process.state", "process.log"
        nlohmann::json payload = nlohmann::json::object();
    };

    // Encode to / decode from a frame's bytes. The decoders throw std::exception
    // (nlohmann parse_error or out-of-range) on a malformed frame; callers map
    // that to a protocol-level error.
    std::string encode(const Request &request);
    std::string encode(const Response &response);
    std::string encode(const Event &event);
    Request decode_request(std::string_view frame);
    Response decode_response(std::string_view frame);

    // A client connection receives either a Response (a reply it correlates by
    // id) or an Event (an unsolicited push). Parse the frame once, classify it,
    // then decode the matching shape.
    bool is_event(const nlohmann::json &frame);
    Response decode_response(const nlohmann::json &frame);
    Event decode_event(const nlohmann::json &frame);
}
