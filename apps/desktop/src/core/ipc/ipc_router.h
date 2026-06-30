#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include "include/cef_browser.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_message_router.h"

namespace desktop::ipc {

// Incoming request from JavaScript.
struct Request {
    std::string channel;
    std::string payload_raw;  // raw JSON value (may be null, string, object, …)

    // Unwrap a top-level JSON string payload; otherwise return the raw JSON.
    std::string PayloadString() const;
};

// Copyable response handle — holds the callback alive across thread boundaries.
class Response {
public:
    explicit Response(CefRefPtr<CefMessageRouterBrowserSide::Callback> cb)
        : cb_(cb) {}

    void Success(const std::string& json) const;
    void Failure(int code, const std::string& msg) const;

private:
    CefRefPtr<CefMessageRouterBrowserSide::Callback> cb_;
};

using Handler = std::function<void(const Request&, Response)>;

// Registers feature handlers under a shared channel prefix.
// Usage:  Actions on("feature_name"); on("action", handler);
// The registered channel will be "feature_name.action".
class Actions {
public:
    Actions(std::string prefix, class Registry& reg);
    void operator()(const std::string& name, Handler handler);

private:
    std::string prefix_;
    class Registry& registry_;
};

// Global singleton mapping channel names → handlers.
class Registry {
public:
    static Registry& Instance();
    void Register(const std::string& channel, Handler handler);
    bool Handle(const std::string& channel, const Request& req, Response res) const;

private:
    std::unordered_map<std::string, Handler> handlers_;
};

// Initialize the browser-side message router (call once in OnContextInitialized).
void Init();

// Access the browser-side router (for browser/Client wiring).
CefRefPtr<CefMessageRouterBrowserSide> GetBrowserRouter();

// Push a native→JS event (dispatched as a CustomEvent on window in the renderer).
void Emit(CefRefPtr<CefBrowser> browser,
          const std::string& channel,
          const std::string& payload_json);

std::string Str(const std::string& s);
std::string Int(int n);
std::string Bool(bool b);
std::string Null();
std::string Dict(CefRefPtr<CefDictionaryValue> d);

}  // namespace desktop::ipc
