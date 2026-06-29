#include "hestia/ipc/protocol.h"

namespace hestia::ipc {
    using nlohmann::json;

    Response Response::success(json payload) {
        Response r;
        r.ok = true;
        r.payload = std::move(payload);
        return r;
    }

    Response Response::failure(std::string code, std::string message) {
        Response r;
        r.ok = false;
        r.error = Error{std::move(code), std::move(message)};
        return r;
    }

    std::string encode(const Request &request) {
        json j;
        j["v"] = request.version;
        j["channel"] = request.channel;
        j["payload"] = request.payload;
        if (request.id) j["id"] = *request.id;
        return j.dump();
    }

    std::string encode(const Event &event) {
        return json{{"event", event.topic}, {"payload", event.payload}}.dump();
    }

    std::string encode(const Response &response) {
        json j;
        j["v"] = response.version;
        j["ok"] = response.ok;
        if (response.ok) {
            j["payload"] = response.payload;
        } else {
            j["error"] = {
                {"code", response.error ? response.error->code : "error"},
                {"message", response.error ? response.error->message : ""},
            };
        }
        if (response.id) j["id"] = *response.id;
        return j.dump();
    }

    Request decode_request(std::string_view frame) {
        const json j = json::parse(frame);
        Request r;
        r.version = j.value("v", kProtocolVersion);
        r.channel = j.at("channel").get<std::string>();
        if (j.contains("payload") && !j["payload"].is_null()) r.payload = j["payload"];
        if (j.contains("id") && j["id"].is_number_integer()) r.id = j["id"].get<long long>();
        return r;
    }

    Response decode_response(const json &j) {
        Response r;
        r.version = j.value("v", kProtocolVersion);
        r.ok = j.value("ok", false);
        if (r.ok) {
            r.payload = j.value("payload", json::object());
        } else {
            const json e = j.value("error", json::object());
            r.error = Error{e.value("code", "error"), e.value("message", "")};
        }
        if (j.contains("id") && j["id"].is_number_integer()) r.id = j["id"].get<long long>();
        return r;
    }

    Response decode_response(std::string_view frame) {
        return decode_response(json::parse(frame));
    }

    bool is_event(const json &frame) {
        return frame.contains("event") && frame["event"].is_string();
    }

    Event decode_event(const json &frame) {
        Event e;
        e.topic = frame.value("event", std::string{});
        if (frame.contains("payload") && !frame["payload"].is_null()) e.payload = frame["payload"];
        return e;
    }
}
