#ifndef NY_RUNTIME_COMMON_H
#define NY_RUNTIME_COMMON_H

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef near
#undef far
#undef None
#undef Quit
#undef KeyDown
#undef KeyUp
#undef MouseMove
#undef MouseDown
#undef MouseUp
#undef DELETE
#undef ERROR
#undef TRANSPARENT
#define TRANSPARENT 0x00FFFFFF
#endif

#include <any>
#include <cassert>
#include <memory>
#include <stdexcept>
#include <cstring>
#include <queue>
#include <map>
#include <mutex>
#include <condition_variable>
#include <chrono>
#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif

#include <functional>
#include <iostream>
#include <cmath>

struct FaultBase : public std::exception {
    virtual const char* what() const noexcept override { return "ML Fault"; }
};

template<typename T>
struct FaultWrapper : public FaultBase {
    T payload;
    FaultWrapper(T p) : payload(p) {}
    const char* what() const noexcept override { return "ML Fault Wrapper"; }
};

struct ny_recover_guard {
    std::function<void()> f;
    ny_recover_guard(std::function<void()> func) : f(std::move(func)) {}
    ~ny_recover_guard() { if (f) f(); }
};

using u32 = uint32_t;
using any = std::any;
using string = std::string;

namespace math {
    inline double cos(double x) { return std::cos(x); }
    inline double sin(double x) { return std::sin(x); }
    inline double tan(double x) { return std::tan(x); }
    inline double sqrt(double x) { return std::sqrt(x); }
    inline double abs(double x) { return std::abs(x); }
    inline double floor(double x) { return std::floor(x); }
    inline double ceil(double x) { return std::ceil(x); }
    inline double round(double x) { return std::round(x); }
    inline double trunc(double x) { return std::trunc(x); }
    inline double pow(double x, double y) { return std::pow(x, y); }
    inline double cbrt(double x) { return std::cbrt(x); }
    inline double exp(double x) { return std::exp(x); }
    inline double log(double x) { return std::log(x); }
    inline double log10(double x) { return std::log10(x); }
    inline double log2(double x) { return std::log2(x); }
    inline double asin(double x) { return std::asin(x); }
    inline double acos(double x) { return std::acos(x); }
    inline double atan(double x) { return std::atan(x); }
    inline double atan2(double y, double x) { return std::atan2(y, x); }
}

#ifdef _WIN32  // Windows-specific GUI code
const char* WND_CLASS_NAME = "NY_MIMIC_WND";

extern "C" {
    static int32_t g_last_key = 0;
    int32_t ny_mimic_init() { return 1; }
    int32_t ny_mimic_poll() {
        MSG msg;
        if (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 1; // Quit
            if (msg.message == WM_KEYDOWN) {
                g_last_key = (int32_t)msg.wParam;
                return 2; // KeyDown
            }
            if (msg.message == WM_KEYUP) {
                g_last_key = (int32_t)msg.wParam;
                return 3; // KeyUp
            }
            if (msg.message == WM_MOUSEMOVE) return 4; // MouseMove
            if (msg.message == WM_LBUTTONDOWN) return 5; // MouseDown
            if (msg.message == WM_LBUTTONUP) return 6; // MouseUp
            if (msg.message == WM_RBUTTONDOWN) return 5; // MouseDown
            if (msg.message == WM_RBUTTONUP) return 6; // MouseUp
            if (msg.message == WM_MBUTTONDOWN) return 5; // MouseDown
            if (msg.message == WM_MBUTTONUP) return 6; // MouseUp
        }
        return 0;
    }
}

// Windows file stubs
extern "C" {
    int ny_file_open(const char* path, int flags) { return -1; }
    int ny_file_close(int fd) { return 0; }
    int ny_file_read(int fd, void* buf, size_t count) { return -1; }
    int ny_file_write(int fd, const void* buf, size_t count) { return -1; }
}

// File functions
int sys_write(int fd, const void* buf, size_t count) {
    return ny_file_write(fd, buf, count);
}

int lseek(int fd, long offset, int whence) {
    return 0; // Stub
}
#endif

// Global theme functions
inline int32_t text_secondary() { return 0xaaaaaa; }
inline int32_t text_selected() { return 0xffff00; }

#endif
#include <iostream>
#include <string>
#include <cstdint>
#include <vector>
#include <array>
#include <tuple>
#include <utility>
#include <functional>

// Forward Declarations
namespace winplat {

struct event;
} // namespace winplat

namespace visual_test {

} // namespace visual_test

namespace winplat {

inline int32_t create_window_impl(string, int32_t, int32_t);
extern "C" void destroy_window_impl(int32_t);
extern "C" int32_t begin_draw_impl(int32_t);
extern "C" void clear_window_impl(int32_t, int32_t);
extern "C" void end_draw_impl(int32_t);
extern "C" int32_t pop_event_impl();
extern "C" int32_t get_event_type_impl();
extern "C" int32_t get_event_key_impl();
extern "C" int32_t get_event_x_impl();
extern "C" int32_t get_event_y_impl();
extern "C" int32_t get_event_modifiers_impl();
extern "C" int32_t show_window_impl(int32_t);
extern "C" void poll_events_impl();
extern "C" void fill_rect_impl(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
inline void draw_text_impl(int32_t, int32_t, int32_t, string, int32_t);
extern "C" int32_t should_close_impl();
extern "C" int32_t get_simulated_time_ms_impl();
inline void set_clipboard_text_impl(string);
inline string get_clipboard_text_impl();
int32_t create_window(string, int32_t, int32_t);
void destroy_window(int32_t);
int32_t begin_draw(int32_t);
void clear_window(int32_t, int32_t);
void end_draw(int32_t);
event get_next_event();
int32_t show_window(int32_t);
void poll_events();
bool should_close();
int32_t get_simulated_time_ms();
void set_clipboard_text(string);
string get_clipboard_text();
int32_t EVENT_KEY_PRESS();
int32_t EVENT_MOUSE_MOVE();
int32_t EVENT_WINDOW_CLOSE();
int32_t EVENT_MOUSE_CLICK();
int32_t EVENT_WINDOW_RESIZE();
int32_t EVENT_MOUSE_PRESS();
int32_t EVENT_MOUSE_RELEASE();
int32_t EVENT_MOUSE_MOTION();
int32_t EVENT_MOUSE_WHEEL();
int32_t MODIFIER_SHIFT();
int32_t MODIFIER_CTRL();
int32_t MODIFIER_ALT();
void fill_rect(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
void draw_text(int32_t, int32_t, int32_t, string, int32_t);
inline string read_file_impl(string);
inline int32_t write_file_impl(string, string);
inline int32_t file_exists_impl(string);
inline string list_directory_impl(string);
string read_file(string);
bool write_file(string, string);
bool file_exists(string);
string list_directory(string);
} // namespace winplat

namespace visual_test {

int main();
} // namespace visual_test

namespace winplat {

struct event {
    int32_t event_type;
    int32_t key_code;
    int32_t mouse_x;
    int32_t mouse_y;
    int32_t width;
    int32_t height;
    int32_t modifiers;
};

} // namespace winplat

namespace visual_test {

} // namespace visual_test

namespace winplat {

namespace ffi {
    extern "C" int32_t create_window_impl(const char* title, int32_t width, int32_t height);
}

inline int32_t create_window_impl(string title, int32_t width, int32_t height) {
    return ffi::create_window_impl(title.c_str(), width, height);
}

extern "C" void destroy_window_impl(int32_t handle);

extern "C" int32_t begin_draw_impl(int32_t handle);

extern "C" void clear_window_impl(int32_t dc, int32_t color);

extern "C" void end_draw_impl(int32_t dc);

extern "C" int32_t pop_event_impl();

extern "C" int32_t get_event_type_impl();

extern "C" int32_t get_event_key_impl();

extern "C" int32_t get_event_x_impl();

extern "C" int32_t get_event_y_impl();

extern "C" int32_t get_event_modifiers_impl();

extern "C" int32_t show_window_impl(int32_t handle);

extern "C" void poll_events_impl();

extern "C" void fill_rect_impl(int32_t handle, int32_t x, int32_t y, int32_t width, int32_t height, int32_t color);

namespace ffi {
    extern "C" void draw_text_impl(int32_t handle, int32_t x, int32_t y, const char* text_str, int32_t color);
}

inline void draw_text_impl(int32_t handle, int32_t x, int32_t y, string text_str, int32_t color) {
    return ffi::draw_text_impl(handle, x, y, text_str.c_str(), color);
}

extern "C" int32_t should_close_impl();

extern "C" int32_t get_simulated_time_ms_impl();

namespace ffi {
    extern "C" void set_clipboard_text_impl(const char* text);
}

inline void set_clipboard_text_impl(string text) {
    return ffi::set_clipboard_text_impl(text.c_str());
}

namespace ffi {
    extern "C" const char* get_clipboard_text_impl();
}

inline string get_clipboard_text_impl() {
    return std::string(ffi::get_clipboard_text_impl());
}

int32_t create_window(string title, int32_t width, int32_t height) {
    {
    return create_window_impl(title, width, height);
    }
}

void destroy_window(int32_t handle) {
    {
    destroy_window_impl(handle);
    }
}

int32_t begin_draw(int32_t handle) {
    {
    return begin_draw_impl(handle);
    }
}

void clear_window(int32_t dc, int32_t color) {
    {
    clear_window_impl(dc, color);
    }
}

void end_draw(int32_t dc) {
    {
    end_draw_impl(dc);
    }
}

event get_next_event() {
    {
    if ((pop_event_impl() == 0)) {
    {
    return winplat::event{.event_type = 0, .key_code = 0, .mouse_x = 0, .mouse_y = 0, .width = 0, .height = 0, .modifiers = 0};
    }
    }
    return winplat::event{.event_type = get_event_type_impl(), .key_code = get_event_key_impl(), .mouse_x = get_event_x_impl(), .mouse_y = get_event_y_impl(), .width = 0, .height = 0, .modifiers = get_event_modifiers_impl()};
    }
}

int32_t show_window(int32_t handle) {
    {
    return show_window_impl(handle);
    }
}

void poll_events() {
    {
    poll_events_impl();
    }
}

bool should_close() {
    {
    return (should_close_impl() != 0);
    }
}

int32_t get_simulated_time_ms() {
    {
    return get_simulated_time_ms_impl();
    }
}

void set_clipboard_text(string text) {
    {
    set_clipboard_text_impl(text);
    }
}

string get_clipboard_text() {
    {
    return get_clipboard_text_impl();
    }
}

int32_t EVENT_KEY_PRESS() {
    {
    return 1;
    }
}

int32_t EVENT_MOUSE_MOVE() {
    {
    return 2;
    }
}

int32_t EVENT_WINDOW_CLOSE() {
    {
    return 3;
    }
}

int32_t EVENT_MOUSE_CLICK() {
    {
    return 4;
    }
}

int32_t EVENT_WINDOW_RESIZE() {
    {
    return 5;
    }
}

int32_t EVENT_MOUSE_PRESS() {
    {
    return 6;
    }
}

int32_t EVENT_MOUSE_RELEASE() {
    {
    return 7;
    }
}

int32_t EVENT_MOUSE_MOTION() {
    {
    return 8;
    }
}

int32_t EVENT_MOUSE_WHEEL() {
    {
    return 9;
    }
}

int32_t MODIFIER_SHIFT() {
    {
    return 1;
    }
}

int32_t MODIFIER_CTRL() {
    {
    return 2;
    }
}

int32_t MODIFIER_ALT() {
    {
    return 4;
    }
}

void fill_rect(int32_t handle, int32_t x, int32_t y, int32_t width, int32_t height, int32_t color) {
    {
    fill_rect_impl(handle, x, y, width, height, color);
    }
}

void draw_text(int32_t handle, int32_t x, int32_t y, string text_str, int32_t color) {
    {
    draw_text_impl(handle, x, y, text_str, color);
    }
}

namespace ffi {
    extern "C" const char* read_file_impl(const char* path);
}

inline string read_file_impl(string path) {
    return std::string(ffi::read_file_impl(path.c_str()));
}

namespace ffi {
    extern "C" int32_t write_file_impl(const char* path, const char* content);
}

inline int32_t write_file_impl(string path, string content) {
    return ffi::write_file_impl(path.c_str(), content.c_str());
}

namespace ffi {
    extern "C" int32_t file_exists_impl(const char* path);
}

inline int32_t file_exists_impl(string path) {
    return ffi::file_exists_impl(path.c_str());
}

namespace ffi {
    extern "C" const char* list_directory_impl(const char* path);
}

inline string list_directory_impl(string path) {
    return std::string(ffi::list_directory_impl(path.c_str()));
}

string read_file(string path) {
    return read_file_impl(path);
}

bool write_file(string path, string content) {
    return (write_file_impl(path, content) != 0);
}

bool file_exists(string path) {
    return (file_exists_impl(path) != 0);
}

string list_directory(string path) {
    {
    return list_directory_impl(path);
    }
}

} // namespace winplat

namespace visual_test {

int main() {
    {
    auto handle = winplat::create_window(std::string("NYCromicon IDE - Visual Test"), 1024, 700);
    if ((handle == 0)) {
    {
    return 0;
    }
    }
    winplat::show_window(handle);
    auto bg_color = 0x1E1E2E;
    auto toolbar_color = 0x181825;
    auto sidebar_color = 0x1E1E2E;
    auto editor_color = 0x1E1E2E;
    auto statusbar_color = 0x181825;
    auto border_color = 0x45475A;
    auto text_color = 0xCDD6F4;
    auto accent_color = 0x89B4FA;
    auto tab_active_color = 0x313244;
    auto tab_inactive_color = 0x1E1E2E;
    auto gutter_color = 0x181825;
    auto line_num_color = 0x6C7086;
    auto keyword_color = 0xCBA6F7;
    auto string_color = 0xA6E3A1;
    auto comment_color = 0x6C7086;
    auto selection_color = 0x45475A;
    auto cursor_color = 0xF5E0DC;
    auto treeview_arrow_color = 0x89B4FA;
    auto toolbar_height = 32;
    auto tabbar_height = 28;
    auto sidebar_width = 200;
    auto statusbar_height = 24;
    auto gutter_width = 48;
    auto line_height = 16;
    auto char_width = 8;
    auto running = true;
    auto mouse_x = 0;
    auto mouse_y = 0;
    auto frame_count = 0;
    while (running) {
    {
    winplat::poll_events();
    if (winplat::should_close()) {
    {
    running = false;
    }
    }
    auto evt = winplat::get_next_event();
    while ((evt.event_type != 0)) {
    {
    if ((evt.event_type == winplat::EVENT_WINDOW_CLOSE())) {
    {
    running = false;
    }
    } else {
    if ((evt.event_type == winplat::EVENT_MOUSE_MOTION())) {
    {
    mouse_x = evt.mouse_x;
    mouse_y = evt.mouse_y;
    }
    } else {
    if ((evt.event_type == winplat::EVENT_KEY_PRESS())) {
    {
    if ((evt.key_code == 27)) {
    {
    running = false;
    }
    }
    }
    }
    }
    }
    evt = winplat::get_next_event();
    }
    }
    auto dc = winplat::begin_draw(handle);
    winplat::clear_window(dc, bg_color);
    winplat::fill_rect(handle, 0, 0, 1024, toolbar_height, toolbar_color);
    winplat::fill_rect(handle, 8, 6, 50, 20, accent_color);
    winplat::draw_text(handle, 14, 8, std::string("New"), 0x1E1E2E);
    winplat::fill_rect(handle, 66, 6, 50, 20, accent_color);
    winplat::draw_text(handle, 72, 8, std::string("Open"), 0x1E1E2E);
    winplat::fill_rect(handle, 124, 6, 50, 20, accent_color);
    winplat::draw_text(handle, 130, 8, std::string("Save"), 0x1E1E2E);
    winplat::fill_rect(handle, 190, 6, 50, 20, accent_color);
    winplat::draw_text(handle, 196, 8, std::string("Build"), 0x1E1E2E);
    winplat::fill_rect(handle, 248, 6, 40, 20, accent_color);
    winplat::draw_text(handle, 254, 8, std::string("Run"), 0x1E1E2E);
    winplat::fill_rect(handle, 0, (toolbar_height - 1), 1024, 1, border_color);
    auto tab_y = toolbar_height;
    winplat::fill_rect(handle, sidebar_width, tab_y, 824, tabbar_height, toolbar_color);
    winplat::fill_rect(handle, sidebar_width, tab_y, 120, tabbar_height, tab_active_color);
    winplat::draw_text(handle, (sidebar_width + 8), (tab_y + 6), std::string("main.ny"), text_color);
    winplat::draw_text(handle, (sidebar_width + 104), (tab_y + 6), std::string("x"), 0x6C7086);
    winplat::fill_rect(handle, (sidebar_width + 120), tab_y, 120, tabbar_height, tab_inactive_color);
    winplat::draw_text(handle, (sidebar_width + 128), (tab_y + 6), std::string("widget.ny"), 0x6C7086);
    winplat::fill_rect(handle, sidebar_width, ((tab_y + tabbar_height) - 1), 824, 1, border_color);
    auto content_y = toolbar_height;
    auto content_height = ((700 - toolbar_height) - statusbar_height);
    winplat::fill_rect(handle, 0, content_y, sidebar_width, content_height, sidebar_color);
    winplat::fill_rect(handle, 0, content_y, sidebar_width, 24, toolbar_color);
    winplat::draw_text(handle, 8, (content_y + 4), std::string("EXPLORER"), 0x6C7086);
    auto tree_y = (content_y + 28);
    winplat::draw_text(handle, 8, tree_y, std::string(">"), treeview_arrow_color);
    winplat::draw_text(handle, 20, tree_y, std::string("nycromicon/"), text_color);
    winplat::draw_text(handle, 24, (tree_y + 18), std::string(">"), treeview_arrow_color);
    winplat::draw_text(handle, 36, (tree_y + 18), std::string("src/"), text_color);
    winplat::fill_rect(handle, 0, (tree_y + 36), sidebar_width, 16, selection_color);
    winplat::draw_text(handle, 48, (tree_y + 36), std::string("main.ny"), accent_color);
    winplat::draw_text(handle, 48, (tree_y + 54), std::string("editor.ny"), 0x6C7086);
    winplat::draw_text(handle, 8, (tree_y + 76), std::string(">"), treeview_arrow_color);
    winplat::draw_text(handle, 20, (tree_y + 76), std::string("nilaix/"), text_color);
    winplat::draw_text(handle, 8, (tree_y + 94), std::string(">"), treeview_arrow_color);
    winplat::draw_text(handle, 20, (tree_y + 94), std::string("nillibrary/"), text_color);
    winplat::fill_rect(handle, (sidebar_width - 1), content_y, 1, content_height, border_color);
    auto editor_x = sidebar_width;
    auto editor_y = (toolbar_height + tabbar_height);
    auto editor_width = (1024 - sidebar_width);
    auto editor_height = (content_height - tabbar_height);
    winplat::fill_rect(handle, editor_x, editor_y, editor_width, editor_height, editor_color);
    winplat::fill_rect(handle, editor_x, editor_y, gutter_width, editor_height, gutter_color);
    auto line_y = (editor_y + 4);
    auto code_x = ((editor_x + gutter_width) + 8);
    winplat::draw_text(handle, (editor_x + 16), line_y, std::string("1"), line_num_color);
    winplat::draw_text(handle, code_x, line_y, std::string("module"), keyword_color);
    winplat::draw_text(handle, (code_x + 56), line_y, std::string("nycromicon"), text_color);
    line_y = (line_y + line_height);
    winplat::draw_text(handle, (editor_x + 16), line_y, std::string("2"), line_num_color);
    winplat::draw_text(handle, code_x, line_y, std::string("import"), keyword_color);
    winplat::draw_text(handle, (code_x + 56), line_y, std::string("winplat"), text_color);
    line_y = (line_y + line_height);
    winplat::draw_text(handle, (editor_x + 16), line_y, std::string("3"), line_num_color);
    winplat::draw_text(handle, code_x, line_y, std::string("import"), keyword_color);
    winplat::draw_text(handle, (code_x + 56), line_y, std::string("widget"), text_color);
    line_y = (line_y + line_height);
    winplat::draw_text(handle, (editor_x + 16), line_y, std::string("4"), line_num_color);
    line_y = (line_y + line_height);
    winplat::draw_text(handle, (editor_x + 16), line_y, std::string("5"), line_num_color);
    winplat::draw_text(handle, code_x, line_y, std::string("// NYCromicon IDE"), comment_color);
    line_y = (line_y + line_height);
    winplat::draw_text(handle, (editor_x + 16), line_y, std::string("6"), line_num_color);
    winplat::draw_text(handle, code_x, line_y, std::string("pub"), keyword_color);
    winplat::draw_text(handle, (code_x + 32), line_y, std::string("function"), keyword_color);
    winplat::draw_text(handle, (code_x + 104), line_y, std::string("main()"), text_color);
    line_y = (line_y + line_height);
    winplat::draw_text(handle, (editor_x + 16), line_y, std::string("7"), line_num_color);
    winplat::draw_text(handle, (code_x + 32), line_y, std::string("let"), keyword_color);
    winplat::draw_text(handle, (code_x + 56), line_y, std::string("title ="), text_color);
    winplat::draw_text(handle, (code_x + 120), line_y, std::string("NYCromicon"), string_color);
    line_y = (line_y + line_height);
    winplat::draw_text(handle, (editor_x + 16), line_y, std::string("8"), line_num_color);
    winplat::draw_text(handle, (code_x + 32), line_y, std::string("let"), keyword_color);
    winplat::draw_text(handle, (code_x + 56), line_y, std::string("w = winplat.create_window(title, 1200, 800)"), text_color);
    line_y = (line_y + line_height);
    winplat::draw_text(handle, (editor_x + 16), line_y, std::string("9"), line_num_color);
    winplat::draw_text(handle, (code_x + 32), line_y, std::string("winplat.show_window(w)"), text_color);
    line_y = (line_y + line_height);
    winplat::draw_text(handle, (editor_x + 8), line_y, std::string("10"), line_num_color);
    winplat::draw_text(handle, code_x, line_y, std::string("end"), keyword_color);
    winplat::draw_text(handle, (code_x + 32), line_y, std::string("function"), keyword_color);
    line_y = (line_y + line_height);
    if ((((frame_count / 30) - (((frame_count / 30) / 2) * 2)) == 0)) {
    {
    winplat::fill_rect(handle, (code_x + 56), ((editor_y + 4) + (6 * line_height)), 2, line_height, cursor_color);
    }
    }
    auto sb_y = (700 - statusbar_height);
    winplat::fill_rect(handle, 0, sb_y, 1024, statusbar_height, statusbar_color);
    winplat::fill_rect(handle, 0, sb_y, 1024, 1, border_color);
    winplat::draw_text(handle, 8, (sb_y + 4), std::string("Ln 7, Col 5"), text_color);
    winplat::draw_text(handle, 200, (sb_y + 4), std::string("main.ny"), 0x6C7086);
    winplat::draw_text(handle, 400, (sb_y + 4), std::string("NY Language"), 0x6C7086);
    winplat::draw_text(handle, 800, (sb_y + 4), std::string("NYCromicon v4.0"), accent_color);
    winplat::end_draw(dc);
    frame_count = (frame_count + 1);
    }
    }
    winplat::destroy_window(handle);
    }
    return 0;
}

} // namespace visual_test

void __ny_test_init() {
}

int main(int argc, char** argv) {
    __ny_test_init();
    return visual_test::main();
}
