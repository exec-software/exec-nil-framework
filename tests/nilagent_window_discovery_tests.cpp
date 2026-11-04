#include "../nilagent/window_discovery.hpp"

#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {
unsigned checks = 0;
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
    ++checks;
}

void predicate_contract() {
    using ny::agent::window_title_matches;
    check(window_title_matches("", ""), "empty title/filter");
    check(window_title_matches("Editor - Project", ""), "empty filter includes all titles");
    check(window_title_matches("Editor - Project", "Editor - Project"), "exact title");
    check(window_title_matches("Editor - Project", "Editor"), "prefix filter");
    check(window_title_matches("Editor - Project", "Project"), "suffix filter");
    check(window_title_matches("Editor - Project", "tor - Pro"), "middle substring filter");
    check(!window_title_matches("Editor - Project", "editor"), "filter remains case sensitive");
    check(!window_title_matches("Editor - Project", "missing"), "nonmatching filter");
    check(!window_title_matches("", "Editor"), "nonempty filter excludes empty title");
    check(window_title_matches(u8"Editor café 中文", u8"café"), "UTF-8 substring bytes");
    check(!window_title_matches(u8"Editor café 中文", u8"CAFÉ"), "no Unicode case folding");
}

#ifdef _WIN32
struct desktop_unavailable : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct owned_windows {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    std::wstring class_name;
    ATOM atom = 0;
    std::vector<HWND> windows;

    explicit owned_windows(const std::string &name) : class_name(name.begin(), name.end()) {
        WNDCLASSW definition{};
        definition.lpfnWndProc = DefWindowProcW;
        definition.hInstance = instance;
        definition.lpszClassName = class_name.c_str();
        atom = RegisterClassW(&definition);
        check(atom != 0, "register private fixture window class");
    }
    ~owned_windows() {
        for (auto hwnd : windows) DestroyWindow(hwnd);
        if (atom) UnregisterClassW(class_name.c_str(), instance);
    }
    HWND add(const std::string &title, bool visible) {
        const std::wstring wide(title.begin(), title.end());
        // Never activate a window, send input, or change another process's UI.
        const auto hwnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            class_name.c_str(), wide.c_str(), WS_OVERLAPPEDWINDOW | (visible ? WS_VISIBLE : 0),
            0, 0, 160, 80, nullptr, nullptr, instance, nullptr);
        check(hwnd != nullptr, "create private fixture window");
        windows.push_back(hwnd);
        DWORD owner = 0;
        check(GetWindowThreadProcessId(hwnd, &owner) != 0 && owner == GetCurrentProcessId(),
              "fixture HWND must belong to this test");
        return hwnd;
    }
    void close() {
        while (!windows.empty()) {
            check(DestroyWindow(windows.back()) != 0, "destroy owned fixture window");
            windows.pop_back();
        }
        check(UnregisterClassW(class_name.c_str(), instance) != 0, "unregister fixture class");
        atom = 0;
    }
};

std::set<std::uintptr_t> identifiers(const std::vector<std::map<std::string, std::string>> &rows) {
    std::set<std::uintptr_t> result;
    for (const auto &row : rows) result.insert(static_cast<std::uintptr_t>(std::stoull(row.at("id"))));
    return result;
}

void windows_contract() {
    const auto nonce = "NILWindowFilter-" + std::to_string(GetCurrentProcessId()) + "-" +
                       std::to_string(GetTickCount64());
    owned_windows fixture(nonce);
    const auto needle = nonce + "-Needle";
    const auto title = "before " + needle + " after";
    const HWND matching = fixture.add(title, true);
    const HWND other = fixture.add("before " + nonce + "-Other after", true);
    const HWND hidden = fixture.add(needle + "-Hidden", false);
    const auto matching_id = reinterpret_cast<std::uintptr_t>(matching);
    const auto other_id = reinterpret_cast<std::uintptr_t>(other);
    const auto hidden_id = reinterpret_cast<std::uintptr_t>(hidden);
    USEROBJECTFLAGS station_flags{};
    DWORD station_bytes = 0;
    check(GetUserObjectInformationW(GetProcessWindowStation(), UOI_FLAGS, &station_flags,
                                   sizeof(station_flags), &station_bytes) != 0,
          "query current window station visibility");
    struct owned_presence {
        HWND matching, other, hidden;
        unsigned count = 0;
    } presence{matching, other, hidden};
    check(EnumWindows([](HWND hwnd, LPARAM parameter) -> BOOL {
        auto &owned = *reinterpret_cast<owned_presence *>(parameter);
        if (hwnd == owned.matching || hwnd == owned.other || hwnd == owned.hidden) ++owned.count;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&presence)) != 0, "independently enumerate owned fixture HWNDs");
    check(presence.count == 3, "all owned fixture HWNDs must be enumerable");
    const bool station_visible = (station_flags.dwFlags & WSF_VISIBLE) != 0;
    const bool matching_visible = IsWindowVisible(matching) != 0;
    const bool other_visible = IsWindowVisible(other) != 0;
    if (!station_visible && !matching_visible && !other_visible) {
        // A service window station can create/enumerate these HWNDs while making
        // even WS_VISIBLE fixtures nonvisible. Production correctly excludes them.
        fixture.close();
        throw desktop_unavailable("desktop unavailable (visible_station=0, owned_enumerated=3, visible_owned=0)");
    }
    check(matching_visible && other_visible, "requested-visible owned fixture HWNDs must be visible");
    using ny::agent::discover_windows;
    const auto filtered = discover_windows(needle);
    check(filtered.size() == 1 && identifiers(filtered) == std::set<std::uintptr_t>{matching_id},
          "production enumerator applies substring filter and excludes hidden windows");
    check(filtered.front().at("title") == title && filtered.front().at("visible") == "true",
          "filtered window retains its title and visibility");
    check(identifiers(discover_windows(title)) == std::set<std::uintptr_t>{matching_id}, "exact-title discovery");
    check(identifiers(discover_windows(nonce)) == std::set<std::uintptr_t>{matching_id, other_id},
          "common substring includes both owned visible windows");
    check(discover_windows(nonce + "-needle").empty(), "production filter remains case sensitive");
    check(discover_windows(nonce + "-Missing").empty(), "nonmatching filter returns no window");
    check(discover_windows(needle + "-Hidden").empty(), "matching hidden window stays excluded");
    const auto unfiltered = identifiers(discover_windows(""));
    check(unfiltered.count(matching_id) && unfiltered.count(other_id) && !unfiltered.count(hidden_id),
          "empty filter preserves visible-window discovery");
    fixture.close();
}
#endif
} // namespace

int main() {
    try {
        predicate_contract();
#ifdef _WIN32
        windows_contract();
#endif
        std::cout << "nilagent window discovery: " << checks << " checks passed\n";
        return 0;
#ifdef _WIN32
    } catch (const desktop_unavailable &error) {
        std::cout << "nilagent window discovery: " << error.what() << '\n';
        return 77;
#endif
    } catch (const std::exception &error) {
        std::cerr << "nilagent window discovery: " << error.what() << '\n';
        return 1;
    }
}
