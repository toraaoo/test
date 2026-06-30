#include "core/common/app_scheme.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_stream_resource_handler.h"
#include "core/build_config.h"
#include <cmrc/cmrc.hpp>
#include <string>

CMRC_DECLARE(hestia_frontend);

namespace desktop::common {

namespace {

std::string MimeFor(const std::string& path) {
    const auto dot = path.find_last_of('.');
    const std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
    std::string mime = CefGetMimeType(ext);
    if (mime.empty())
        mime = (ext == "js" || ext == "mjs") ? "text/javascript" : "application/octet-stream";
    return mime;
}

class AppSchemeHandlerFactory : public CefSchemeHandlerFactory {
public:
    CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser>,
                                          CefRefPtr<CefFrame>,
                                          const CefString&,
                                          CefRefPtr<CefRequest> request) override {
        auto fs = cmrc::hestia_frontend::get_filesystem();

        CefURLParts parts;
        CefParseURL(request->GetURL(), parts);
        std::string path = CefString(&parts.path).ToString();
        if (!path.empty() && path.front() == '/') path.erase(0, 1);
        if (path.empty()) path = "index.html";

        // SPA fallback: extensionless paths serve the entry point.
        if (!fs.exists(path)) {
            if (path.find('.') == std::string::npos && fs.exists("index.html"))
                path = "index.html";
            else
                return new CefStreamResourceHandler(404, "Not Found", "text/plain", {}, nullptr);
        }

        cmrc::file f = fs.open(path);
        auto stream = CefStreamReader::CreateForData(
            const_cast<char*>(f.begin()), f.size());
        CefResponse::HeaderMap headers;
        headers.emplace("Access-Control-Allow-Origin", "*");
        return new CefStreamResourceHandler(200, "OK", MimeFor(path), headers, stream);
    }

    IMPLEMENT_REFCOUNTING(AppSchemeHandlerFactory);
};

}  // namespace

void RegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
    registrar->AddCustomScheme(APP_SCHEME,
        CEF_SCHEME_OPTION_STANDARD |
        CEF_SCHEME_OPTION_SECURE   |
        CEF_SCHEME_OPTION_CORS_ENABLED |
        CEF_SCHEME_OPTION_FETCH_ENABLED);
}

void RegisterSchemeHandlerFactory() {
    CefRegisterSchemeHandlerFactory(APP_SCHEME, "app", new AppSchemeHandlerFactory());
}

const char* GetSchemeOrigin() {
    return APP_SCHEME "://app/";
}

}  // namespace desktop::common
