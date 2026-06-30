#include "core/ipc/ipc_router.h"
#include "include/cef_parser.h"
#include "include/cef_process_message.h"

namespace desktop::ipc {

void Response::Success(const std::string& json) const {
    if (cb_) cb_->Success(json);
}

void Response::Failure(int code, const std::string& msg) const {
    if (cb_) cb_->Failure(code, msg);
}

std::string Request::PayloadString() const {
    auto v = CefParseJSON(payload_raw, JSON_PARSER_ALLOW_TRAILING_COMMAS);
    if (v && v->GetType() == VTYPE_STRING)
        return v->GetString().ToString();
    return payload_raw;
}

Registry& Registry::Instance() {
    static Registry instance;
    return instance;
}

void Registry::Register(const std::string& channel, Handler handler) {
    handlers_[channel] = std::move(handler);
}

bool Registry::Handle(const std::string& channel,
                      const Request& req,
                      Response res) const {
    auto it = handlers_.find(channel);
    if (it == handlers_.end()) return false;
    it->second(req, std::move(res));
    return true;
}

Actions::Actions(std::string prefix, Registry& reg)
    : prefix_(std::move(prefix)), registry_(reg) {}

void Actions::operator()(const std::string& name, Handler handler) {
    registry_.Register(prefix_ + "." + name, std::move(handler));
}

namespace {

class RouterHandler : public CefMessageRouterBrowserSide::Handler {
public:
    bool OnQuery(CefRefPtr<CefBrowser> /*browser*/,
                 CefRefPtr<CefFrame>  /*frame*/,
                 int64_t              /*query_id*/,
                 const CefString&     request_str,
                 bool                 /*persistent*/,
                 CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) override {
        auto root = CefParseJSON(request_str, JSON_PARSER_ALLOW_TRAILING_COMMAS);
        if (!root || root->GetType() != VTYPE_DICTIONARY) {
            callback->Failure(-1, "malformed request (expected JSON object)");
            return true;
        }
        auto d = root->GetDictionary();

        Request req;
        req.channel     = d->GetString("channel").ToString();
        auto payload_v  = d->GetValue("payload");
        req.payload_raw = payload_v
            ? CefWriteJSON(payload_v, JSON_WRITER_DEFAULT).ToString()
            : "null";

        Response res(callback);
        if (!Registry::Instance().Handle(req.channel, req, res))
            callback->Failure(-2, "unknown channel: " + req.channel);
        return true;
    }

    void OnQueryCanceled(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                         int64_t) override {}
};

CefRefPtr<CefMessageRouterBrowserSide> g_router;

}  // namespace

void Init() {
    CefMessageRouterConfig cfg;
    cfg.js_query_function  = "cefQuery";
    cfg.js_cancel_function = "cefQueryCancel";
    g_router = CefMessageRouterBrowserSide::Create(cfg);
    g_router->AddHandler(new RouterHandler(), false);
}

CefRefPtr<CefMessageRouterBrowserSide> GetBrowserRouter() {
    return g_router;
}

void Emit(CefRefPtr<CefBrowser> browser,
          const std::string& channel,
          const std::string& payload_json) {
    if (!browser) return;
    auto msg  = CefProcessMessage::Create("hestia.emit");
    auto args = msg->GetArgumentList();
    args->SetString(0, channel);
    args->SetString(1, payload_json);
    browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, msg);
}

std::string Str(const std::string& s) {
    auto v = CefValue::Create();
    v->SetString(s);
    return CefWriteJSON(v, JSON_WRITER_DEFAULT).ToString();
}

std::string Int(int n) {
    auto v = CefValue::Create();
    v->SetInt(n);
    return CefWriteJSON(v, JSON_WRITER_DEFAULT).ToString();
}

std::string Bool(bool b) {
    auto v = CefValue::Create();
    v->SetBool(b);
    return CefWriteJSON(v, JSON_WRITER_DEFAULT).ToString();
}

std::string Null() {
    auto v = CefValue::Create();
    v->SetNull();
    return CefWriteJSON(v, JSON_WRITER_DEFAULT).ToString();
}

std::string Dict(CefRefPtr<CefDictionaryValue> d) {
    auto v = CefValue::Create();
    v->SetDictionary(d);
    return CefWriteJSON(v, JSON_WRITER_DEFAULT).ToString();
}

}  // namespace desktop::ipc
