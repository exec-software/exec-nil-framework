#pragma once

#include <string_view>

namespace ny::agent {
inline bool window_title_matches(std::string_view title, std::string_view filter) {
    return filter.empty() || title.find(filter) != std::string_view::npos;
}
} // namespace ny::agent

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ny::agent {
inline std::vector<std::map<std::string, std::string>> discover_windows(std::string_view filter) {
    struct enumeration {
        std::string_view filter;
        std::vector<std::map<std::string, std::string>> windows;
    } state{filter, {}};
    EnumWindows([](HWND hwnd, LPARAM parameter) -> BOOL {
        auto &state = *reinterpret_cast<enumeration *>(parameter);
        if (!IsWindowVisible(hwnd)) return TRUE;

        char title[256]{};
        GetWindowTextA(hwnd, title, sizeof(title));
        if (!window_title_matches(title, state.filter)) return TRUE;

        RECT rect{};
        if (!GetWindowRect(hwnd, &rect)) return TRUE;
        std::map<std::string, std::string> info;
        info["id"] = std::to_string(reinterpret_cast<std::uintptr_t>(hwnd));
        info["title"] = title;
        info["x"] = std::to_string(rect.left);
        info["y"] = std::to_string(rect.top);
        info["width"] = std::to_string(rect.right - rect.left);
        info["height"] = std::to_string(rect.bottom - rect.top);
        info["visible"] = "true";
        state.windows.push_back(std::move(info));
        return TRUE;
    }, reinterpret_cast<LPARAM>(&state));
    return state.windows;
}
} // namespace ny::agent
#endif
