#include "core/app/browser_app.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/cef_command_line.h"
#include "core/browser/client.h"
#include "core/common/app_scheme.h"
#include "core/common/app_settings.h"
#include "core/ipc/ipc_router.h"
#include "core/window/window_delegate.h"
#include "core/app/tray_launcher.h"
#include "features/feature_registry.h"

namespace desktop::app {

void BrowserApp::OnContextInitialized() {
    common::InitSettings(CefCommandLine::GetGlobalCommandLine());
    common::RegisterSchemeHandlerFactory();
    ipc::Init();
    features::RegisterAll();
    SpawnTray();

    CefBrowserSettings browser_settings;
    auto view = CefBrowserView::CreateBrowserView(
        new browser::Client(),
        common::GetStartupURL(),
        browser_settings,
        nullptr, nullptr,
        new window::BrowserViewDelegate());
    CefWindow::CreateTopLevelWindow(new window::WindowDelegate(view));
}

}  // namespace desktop::app
