#pragma once

#if defined(_WIN32)

#include <string>

#include <windows.h>

namespace hestia::daemon {
    inline std::wstring widen(const std::string &utf8) {
        if (utf8.empty()) return {};
        const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                            static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(n), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                              wide.data(), n);
        return wide;
    }
}

#endif
