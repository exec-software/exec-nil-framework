#include <windows.h>
#include <string>
#include <cstdint>
#include <vector>
#include <fstream>
#include <sstream>

bool window_closed = false;
int mouse_x = 0;
int mouse_y = 0;
bool mouse_clicked = false;
HWND g_hwnd = nullptr;  // global window handle for width/height queries

struct Event {
    int type;
    int key_code;
    int mouse_x;
    int mouse_y;
};

std::vector<Event> event_queue;

extern "C" {

    __declspec(dllexport) int create_window_impl(const char* title, int width, int height) {
        WNDCLASSA wc = {};
        wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
            switch (msg) {
                case WM_CLOSE:
                case WM_DESTROY:
                    window_closed = true;
                    event_queue.push_back({3, 0, 0, 0}); // EVENT_WINDOW_CLOSE
                    PostQuitMessage(0);
                    return 0;
                case WM_LBUTTONDOWN:
                    mouse_x = LOWORD(lParam);
                    mouse_y = HIWORD(lParam);
                    mouse_clicked = true;
                    event_queue.push_back({2, 0, mouse_x, mouse_y}); // EVENT_MOUSE_CLICK
                    return 0;
                case WM_LBUTTONUP:
                    mouse_clicked = false;
                    return 0;
                case WM_MOUSEMOVE:
                    mouse_x = LOWORD(lParam);
                    mouse_y = HIWORD(lParam);
                    return 0;
                case WM_CHAR:
                    event_queue.push_back({1, (int)wParam, 0, 0}); // EVENT_KEY_PRESS
                    return 0;
                case WM_KEYDOWN:
                    if (wParam == VK_BACK || wParam == VK_RETURN || wParam == VK_ESCAPE || wParam == VK_LEFT || wParam == VK_RIGHT) {
                        event_queue.push_back({1, (int)wParam | 0x10000, 0, 0}); // Special keys with a flag
                    }
                    return 0;
                default:
                    return DefWindowProcA(hwnd, msg, wParam, lParam);
            }
        };
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = "NycromiconClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassA(&wc);

        DWORD style = WS_OVERLAPPEDWINDOW;
        RECT rect = {0, 0, width, height};
        AdjustWindowRect(&rect, style, FALSE);

        HWND hwnd = CreateWindowA(
            wc.lpszClassName,
            title,
            style,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left, rect.bottom - rect.top,
            nullptr, nullptr, wc.hInstance, nullptr
        );
        g_hwnd = hwnd;
        return (int)(uintptr_t)hwnd;
    }

    __declspec(dllexport) void destroy_window_impl(int handle) {
        DestroyWindow((HWND)(uintptr_t)handle);
    }

    __declspec(dllexport) int begin_draw_impl(int handle) {
        return (int)(uintptr_t)handle;
    }

    __declspec(dllexport) void clear_window_impl(int handle, int color) {
        HWND hwnd = (HWND)(uintptr_t)handle;
        HDC hdc = GetDC(hwnd);
        RECT rect;
        GetClientRect(hwnd, &rect);
        HBRUSH brush = CreateSolidBrush(RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF));
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
        ReleaseDC(hwnd, hdc);
    }

    __declspec(dllexport) void end_draw_impl(int handle) {
        // No-op for now
    }

    __declspec(dllexport) long long next_event() {
        if (event_queue.empty()) {
            return 0LL;
        }
        Event e = event_queue.front();
        event_queue.erase(event_queue.begin());

        // Pack event data:
        // type: 0-15 (4 bits)
        // key_code: 16-31 (16 bits)
        // mouse_x: 32-47 (16 bits)
        // mouse_y: 48-63 (16 bits)

        return (long long)e.type |
               ((long long)e.key_code << 16) |
               ((long long)e.mouse_x << 32) |
               ((long long)(short)e.mouse_y << 48);
    }

    __declspec(dllexport) int get_mouse_x_impl() {
        return mouse_x;
    }

    __declspec(dllexport) int get_mouse_y_impl() {
        return mouse_y;
    }

    __declspec(dllexport) bool get_mouse_clicked_impl() {
        return mouse_clicked;
    }

    __declspec(dllexport) int show_window_impl(int handle) {
        ShowWindow((HWND)(uintptr_t)handle, SW_SHOW);
        return 0;
    }

    __declspec(dllexport) void poll_events_impl() {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                window_closed = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    __declspec(dllexport) void fill_rect(int handle, int x, int y, int width, int height, int color) {
        HWND hwnd = (HWND)(uintptr_t)handle;
        HDC hdc = GetDC(hwnd);
        RECT rect = {x, y, x + width, y + height};
        HBRUSH brush = CreateSolidBrush(RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF));
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
        ReleaseDC(hwnd, hdc);
    }

    __declspec(dllexport) void draw_text(int handle, int x, int y, const char* text_str, int color) {
        HWND hwnd = (HWND)(uintptr_t)handle;
        HDC hdc = GetDC(hwnd);
        SetTextColor(hdc, RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF));
        SetBkMode(hdc, TRANSPARENT);
        TextOutA(hdc, x, y, text_str, (int)strlen(text_str));
        ReleaseDC(hwnd, hdc);
    }

    Event last_popped_event = {0, 0, 0, 0};

    __declspec(dllexport) int pop_event_impl() {
        if (event_queue.empty()) {
            return 0;
        }
        last_popped_event = event_queue.front();
        event_queue.erase(event_queue.begin());
        return 1;
    }

    __declspec(dllexport) int get_event_type_impl() { return last_popped_event.type; }
    __declspec(dllexport) int get_event_key_impl() { return last_popped_event.key_code; }
    __declspec(dllexport) int get_event_x_impl() { return last_popped_event.mouse_x; }
    __declspec(dllexport) int get_event_y_impl() { return last_popped_event.mouse_y; }

    __declspec(dllexport) int get_simulated_time_ms_impl() {
        return (int)GetTickCount();
    }

    __declspec(dllexport) bool should_close_impl() {
        return window_closed;
    }

    // --- missing window helpers ---

    __declspec(dllexport) int32_t get_event_modifiers_impl() {
        int32_t mods = 0;
        if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= 1;
        if (GetKeyState(VK_CONTROL) & 0x8000) mods |= 2;
        if (GetKeyState(VK_MENU)    & 0x8000) mods |= 4;
        return mods;
    }

    __declspec(dllexport) void wait_events_timeout_impl(int32_t ms) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, (DWORD)ms, QS_ALLINPUT);
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    __declspec(dllexport) void post_empty_event_impl() {
        PostMessageA(g_hwnd, WM_NULL, 0, 0);
    }

    __declspec(dllexport) int32_t get_window_width_impl() {
        if (!g_hwnd) return 0;
        RECT r;
        GetClientRect(g_hwnd, &r);
        return (int32_t)(r.right - r.left);
    }

    __declspec(dllexport) int32_t get_window_height_impl() {
        if (!g_hwnd) return 0;
        RECT r;
        GetClientRect(g_hwnd, &r);
        return (int32_t)(r.bottom - r.top);
    }

    __declspec(dllexport) void fill_rect_impl(int32_t handle, int32_t x, int32_t y, int32_t width, int32_t height, int32_t color) {
        fill_rect(handle, x, y, width, height, color);
    }

    __declspec(dllexport) void draw_text_impl(int32_t handle, int32_t x, int32_t y, const char* text_str, int32_t color) {
        draw_text(handle, x, y, text_str, color);
    }

    // --- clipboard ---

    static std::string g_clipboard_buf;

    __declspec(dllexport) void set_clipboard_text_impl(const char* text) {
        if (!OpenClipboard(nullptr)) return;
        EmptyClipboard();
        size_t len = strlen(text) + 1;
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
        if (hMem) {
            memcpy(GlobalLock(hMem), text, len);
            GlobalUnlock(hMem);
            SetClipboardData(CF_TEXT, hMem);
        }
        CloseClipboard();
    }

    __declspec(dllexport) const char* get_clipboard_text_impl() {
        g_clipboard_buf.clear();
        if (!OpenClipboard(nullptr)) return g_clipboard_buf.c_str();
        HANDLE hData = GetClipboardData(CF_TEXT);
        if (hData) {
            const char* ptr = (const char*)GlobalLock(hData);
            if (ptr) g_clipboard_buf = ptr;
            GlobalUnlock(hData);
        }
        CloseClipboard();
        return g_clipboard_buf.c_str();
    }

    // --- file I/O ---

    static std::string g_file_buf;
    static std::string g_dir_buf;

    __declspec(dllexport) const char* read_file_impl(const char* path) {
        g_file_buf.clear();
        std::ifstream f(path, std::ios::binary);
        if (f) {
            std::ostringstream ss;
            ss << f.rdbuf();
            g_file_buf = ss.str();
        }
        return g_file_buf.c_str();
    }

    __declspec(dllexport) int32_t write_file_impl(const char* path, const char* content) {
        std::ofstream f(path, std::ios::binary);
        if (!f) return 0;
        f << content;
        return f ? 1 : 0;
    }

    __declspec(dllexport) int32_t file_exists_impl(const char* path) {
        DWORD attr = GetFileAttributesA(path);
        return (attr != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
    }

    __declspec(dllexport) const char* list_directory_impl(const char* path) {
        g_dir_buf.clear();
        std::string pattern = std::string(path) + "\\*";
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) return g_dir_buf.c_str();
        bool first = true;
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            if (!first) g_dir_buf += "\n";
            g_dir_buf += fd.cFileName;
            first = false;
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
        return g_dir_buf.c_str();
    }

    // --- string utilities ---

    static std::string g_substr_buf;
    static std::string g_intstr_buf;
    static char g_char_at_buf[2]       = {0, 0};
    static char g_char_from_code_buf[2] = {0, 0};
    static char g_char_from_int_buf[2]  = {0, 0};

    __declspec(dllexport) const char* _substring(const char* s, int32_t start, int32_t length) {
        std::string str(s);
        int32_t sz = (int32_t)str.size();
        if (start < 0) start = 0;
        if (start >= sz) { g_substr_buf.clear(); return g_substr_buf.c_str(); }
        if (length < 0 || start + length > sz) length = sz - start;
        g_substr_buf = str.substr((size_t)start, (size_t)length);
        return g_substr_buf.c_str();
    }

    __declspec(dllexport) int32_t _string_length(const char* s) {
        return (int32_t)strlen(s);
    }

    __declspec(dllexport) const char* _char_at(const char* s, int32_t index) {
        size_t len = strlen(s);
        g_char_at_buf[0] = (index >= 0 && (size_t)index < len) ? s[index] : '\0';
        return g_char_at_buf;
    }

    __declspec(dllexport) int32_t _char_code(const char* c) {
        return (int32_t)(unsigned char)c[0];
    }

    __declspec(dllexport) const char* _char_from_code(int32_t code) {
        g_char_from_code_buf[0] = (char)(unsigned char)(code & 0xFF);
        return g_char_from_code_buf;
    }

    __declspec(dllexport) const char* _int_to_string(int32_t value) {
        g_intstr_buf = std::to_string(value);
        return g_intstr_buf.c_str();
    }

    __declspec(dllexport) const char* _char_from_int(int32_t i) {
        g_char_from_int_buf[0] = (char)(unsigned char)(i & 0xFF);
        return g_char_from_int_buf;
    }

    // --- CLI args ---

    static std::string g_args_buf;

    __declspec(dllexport) const char* _get_args() {
        g_args_buf.clear();
        int argc = 0;
        LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!wargv) return g_args_buf.c_str();
        // Skip index 0 (exe name); return argv[1..N] newline-separated
        for (int i = 1; i < argc; i++) {
            if (i > 1) g_args_buf += "\n";
            int sz = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
            if (sz > 1) {
                std::string tmp(sz - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, &tmp[0], sz, nullptr, nullptr);
                g_args_buf += tmp;
            }
        }
        LocalFree(wargv);
        return g_args_buf.c_str();
    }

    __declspec(dllexport) void _print_line(const char* s) {
        puts(s);
        fflush(stdout);
    }

    __declspec(dllexport) void _print_err(const char* s) {
        fputs(s, stderr);
        fputs("\n", stderr);
        fflush(stderr);
    }

    __declspec(dllexport) void ny_exit(int32_t code) {
        ExitProcess((UINT)code);
    }

    // --- mimic buffer stubs (no-op on Windows native path) ---

    __declspec(dllexport) void ny_mimic_fill_rect_buffer(int32_t handle, int32_t x, int32_t y, int32_t w, int32_t h, int32_t color) {
        fill_rect(handle, x, y, w, h, color);
    }

    __declspec(dllexport) void ny_mimic_draw_line_buffer(int32_t handle, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t color) {
        HWND hwnd = (HWND)(uintptr_t)handle;
        HDC hdc = GetDC(hwnd);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF));
        HGDIOBJ old = SelectObject(hdc, pen);
        MoveToEx(hdc, x1, y1, nullptr);
        LineTo(hdc, x2, y2);
        SelectObject(hdc, old);
        DeleteObject(pen);
        ReleaseDC(hwnd, hdc);
    }

}