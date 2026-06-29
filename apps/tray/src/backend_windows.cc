#include "tray_backend.h"

#include <mutex>
#include <utility>
#include <vector>

#include <windows.h>
#include <shellapi.h>

// Windows tray backend: a notification-area icon via Shell_NotifyIcon, driven by
// a message-only window. The Win32 message loop is the UI thread; set_model()/
// quit() from other threads marshal work in with PostMessage. The menu is built
// on demand when the icon is clicked, from the last model handed to set_model().
namespace hestia::tray {
    namespace {
        constexpr UINT kTrayCallback = WM_APP + 1; // icon mouse events
        constexpr UINT kIconId = 1;
        constexpr UINT kFirstCommandId = 1000; // popup menu command id base

        std::wstring widen(const std::string &s) {
            if (s.empty()) return {};
            const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                                static_cast<int>(s.size()), nullptr, 0);
            std::wstring out(static_cast<std::size_t>(n), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                  out.data(), n);
            return out;
        }

        class ShellNotifyBackend final : public TrayBackend {
        public:
            explicit ShellNotifyBackend(std::string app_name)
                : app_name_(std::move(app_name)) {}

            void set_model(TrayModel model) override {
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    model_ = std::move(model);
                }
                if (hwnd_) ::PostMessageW(hwnd_, kRefreshTip, 0, 0);
            }

            void run() override {
                const HINSTANCE instance = ::GetModuleHandleW(nullptr);
                WNDCLASSEXW wc{};
                wc.cbSize = sizeof(wc);
                wc.lpfnWndProc = &ShellNotifyBackend::wnd_proc;
                wc.hInstance = instance;
                wc.lpszClassName = L"HestiaTrayWindow";
                ::RegisterClassExW(&wc);

                hwnd_ = ::CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0,
                                          HWND_MESSAGE, nullptr, instance, this);

                NOTIFYICONDATAW nid{};
                nid.cbSize = sizeof(nid);
                nid.hWnd = hwnd_;
                nid.uID = kIconId;
                nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
                nid.uCallbackMessage = kTrayCallback;
                nid.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
                update_tip(nid);
                ::Shell_NotifyIconW(NIM_ADD, &nid);

                MSG msg;
                while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
                    ::TranslateMessage(&msg);
                    ::DispatchMessageW(&msg);
                }
            }

            void quit() override {
                if (hwnd_) ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            }

        private:
            static constexpr UINT kRefreshTip = WM_APP + 2;

            void update_tip(NOTIFYICONDATAW &nid) {
                std::wstring tip;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    tip = widen(model_.tooltip.empty() ? app_name_ : model_.tooltip);
                }
                ::wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
            }

            // Show the popup menu and invoke the chosen item's callback. The menu
            // command ids index into a parallel callback list built here.
            void show_menu() {
                TrayModel model;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    model = model_;
                }
                HMENU menu = ::CreatePopupMenu();
                std::vector<std::function<void()>> callbacks;
                for (const auto &item: model.items) {
                    if (item.separator) {
                        ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                        continue;
                    }
                    UINT flags = MF_STRING;
                    if (!item.enabled) flags |= MF_GRAYED;
                    if (item.checked) flags |= MF_CHECKED;
                    UINT id = 0;
                    if (item.on_click) {
                        id = kFirstCommandId + static_cast<UINT>(callbacks.size());
                        callbacks.push_back(item.on_click);
                    }
                    ::AppendMenuW(menu, flags, id, widen(item.label).c_str());
                }

                POINT pt;
                ::GetCursorPos(&pt);
                ::SetForegroundWindow(hwnd_); // so the menu dismisses on focus loss
                const int chosen = ::TrackPopupMenu(
                    menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                    pt.x, pt.y, 0, hwnd_, nullptr);
                ::DestroyMenu(menu);

                if (chosen >= static_cast<int>(kFirstCommandId)) {
                    const std::size_t index = static_cast<std::size_t>(chosen) - kFirstCommandId;
                    if (index < callbacks.size() && callbacks[index]) callbacks[index]();
                }
            }

            static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
                if (msg == WM_NCCREATE) {
                    auto *self = static_cast<ShellNotifyBackend *>(
                        reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
                    ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                        reinterpret_cast<LONG_PTR>(self));
                    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
                }
                auto *self = reinterpret_cast<ShellNotifyBackend *>(
                    ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                if (!self) return ::DefWindowProcW(hwnd, msg, wparam, lparam);

                switch (msg) {
                    case kTrayCallback:
                        if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_LBUTTONUP) {
                            self->show_menu();
                        }
                        return 0;
                    case kRefreshTip: {
                        NOTIFYICONDATAW nid{};
                        nid.cbSize = sizeof(nid);
                        nid.hWnd = hwnd;
                        nid.uID = kIconId;
                        nid.uFlags = NIF_TIP;
                        self->update_tip(nid);
                        ::Shell_NotifyIconW(NIM_MODIFY, &nid);
                        return 0;
                    }
                    case WM_DESTROY: {
                        NOTIFYICONDATAW nid{};
                        nid.cbSize = sizeof(nid);
                        nid.hWnd = hwnd;
                        nid.uID = kIconId;
                        ::Shell_NotifyIconW(NIM_DELETE, &nid);
                        ::PostQuitMessage(0);
                        return 0;
                    }
                    default:
                        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
                }
            }

            std::string app_name_;
            HWND hwnd_ = nullptr;

            std::mutex mu_;
            TrayModel model_;
        };
    } // namespace

    std::unique_ptr<TrayBackend> make_tray_backend(std::string app_name) {
        return std::make_unique<ShellNotifyBackend>(std::move(app_name));
    }
}
