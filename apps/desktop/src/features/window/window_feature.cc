#include "features/window/window_feature.h"
#include "core/ipc/ipc_router.h"
#include "core/window/window_util.h"
#include "core/browser/client_manager.h"

namespace desktop::features {

namespace {

void EmitWindowState(CefRefPtr<CefWindow> win) {
    if (!win) return;
    auto browser = browser::ClientManager::Instance().GetMainBrowser();
    if (!browser) return;

    auto d = CefDictionaryValue::Create();
    d->SetBool("maximized", win->IsMaximized());
    d->SetBool("minimized", window::IsMinimized());
    ipc::Emit(browser, "window.state", ipc::Dict(d));
}

std::string WindowStateJson(CefRefPtr<CefWindow> win) {
    auto d = CefDictionaryValue::Create();
    d->SetBool("maximized", win ? win->IsMaximized() : false);
    d->SetBool("minimized", window::IsMinimized());
    return ipc::Dict(d);
}

}  // namespace

void WindowFeature::RegisterActions(ipc::Actions& on) {
    on("state", [](const ipc::Request&, ipc::Response res) {
        auto win = window::GetActiveWindow();
        res.Success(WindowStateJson(win));
    });

    on("minimize", [](const ipc::Request&, ipc::Response res) {
        auto win = window::GetActiveWindow();
        if (!win) { res.Failure(-1, "no window"); return; }
        window::SetMinimized(true);
        win->Minimize();
        EmitWindowState(win);
        res.Success(ipc::Null());
    });

    on("maximize", [](const ipc::Request&, ipc::Response res) {
        auto win = window::GetActiveWindow();
        if (!win) { res.Failure(-1, "no window"); return; }
        if (win->IsMaximized()) {
            win->Restore();
        } else {
            win->Maximize();
        }
        EmitWindowState(win);
        res.Success(ipc::Null());
    });

    on("close", [](const ipc::Request&, ipc::Response res) {
        auto win = window::GetActiveWindow();
        if (win) win->Close();
        res.Success(ipc::Null());
    });
}

}  // namespace desktop::features
