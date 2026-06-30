#include "core/window/window_delegate.h"
#include "core/window/window_util.h"
#include "core/build_config.h"

namespace desktop::window {

WindowDelegate::WindowDelegate(CefRefPtr<CefBrowserView> view) : view_(view) {}

void WindowDelegate::OnWindowCreated(CefRefPtr<CefWindow> window) {
    SetActiveWindow(window);
    window->SetTitle(APP_NAME);
    window->AddChildView(view_);
    window->Show();
    view_->RequestFocus();
}

void WindowDelegate::OnWindowDestroyed(CefRefPtr<CefWindow> /*window*/) {
    SetActiveWindow(nullptr);
    view_ = nullptr;
}

}  // namespace desktop::window
