/*
 * Copyright (C) 2008 - 2026 Execute Software. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
// Windows provider for the ui.v1 contract, built instead of the GTK provider
// on Windows: one toolkit per platform. The observable contract is the one
// stated in ui_runtime.hpp and shared with the GTK provider through
// ui_runtime_shared.hpp -- identical handles, limits, error text, kind codes
// and event kinds. Only the toolkit beneath it differs.
//
// Win32 has no box or grid container, so containers are real child windows of
// a private class and this file owns the layout pass that USER32 does not
// provide. Keeping containers as windows is what makes visibility and
// sensitivity inherit, and clipping behave, the way the contract describes.
#include "ui_runtime.hpp"
#include "ui_runtime_shared.hpp"

#include <algorithm>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace ny::ui {
namespace {

const wchar_t window_class[] = L"NyUi.Window";
const wchar_t container_class[] = L"NyUi.Container";

// Control metrics. Win32 reports no natural size for a control, so the
// provider measures text with the message font and adds the padding the
// system themes expect. These are layout inputs, not contract values.
constexpr int button_padding_x = 24, button_padding_y = 10;
constexpr int label_padding_x = 2, label_padding_y = 4;
constexpr int entry_padding_x = 10, entry_padding_y = 8;
constexpr int entry_minimum_width = 120;
constexpr int checkbox_box_width = 20;

std::wstring to_wide(const std::string &text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                       static_cast<int>(text.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring wide(static_cast<std::size_t>(size), L'\0');
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                           static_cast<int>(text.size()), wide.data(), size))
    return {};
  return wide;
}

// Returns false rather than a lossy string: callers report invalid text.
bool to_utf8(const std::wstring &wide, std::string &out) {
  if (wide.empty()) { out.clear(); return true; }
  const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                                       static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return false;
  std::string text(static_cast<std::size_t>(size), '\0');
  if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()),
                           text.data(), size, nullptr, nullptr))
    return false;
  out = std::move(text);
  return true;
}

std::string window_text(HWND window) {
  const int length = GetWindowTextLengthW(window);
  if (length <= 0) return {};
  std::wstring wide(static_cast<std::size_t>(length) + 1, L'\0');
  const int copied = GetWindowTextW(window, wide.data(), length + 1);
  wide.resize(copied < 0 ? 0 : static_cast<std::size_t>(copied));
  std::string text;
  return to_utf8(wide, text) ? text : std::string{};
}

// A noninteractive station (a service, or an SSH session without a desktop)
// cannot create a window at all. Refuse before the first CreateWindowExW so
// the failure names the cause instead of surfacing a generic Win32 error.
bool interactive_window_station() noexcept {
  const HWINSTA station = GetProcessWindowStation();
  if (!station) return false;
  USEROBJECTFLAGS flags{};
  DWORD needed = 0;
  if (!GetUserObjectInformationW(station, UOI_FLAGS, &flags, sizeof flags, &needed)) return false;
  return (flags.dwFlags & WSF_VISIBLE) != 0;
}

struct Platform {
  std::mutex thread_mutex;
  std::thread::id ui_thread;
  bool initialized = false;
  bool registered = false;
  HFONT font = nullptr;

  // Message font, so measured text matches what the controls actually render.
  HFONT message_font() {
    if (font) return font;
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof metrics;
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof metrics, &metrics, 0))
      font = CreateFontIndirectW(&metrics.lfMessageFont);
    if (!font) font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    return font;
  }
};
Platform &platform() { static Platform *value = new Platform; return *value; }

SIZE measure(const std::wstring &text) {
  SIZE size{0, 0};
  const HDC screen = GetDC(nullptr);
  if (!screen) return size;
  const HDC memory = CreateCompatibleDC(screen);
  if (memory) {
    const HGDIOBJ previous = SelectObject(memory, platform().message_font());
    // An empty string still needs the font's line height for row sizing.
    const wchar_t *measured = text.empty() ? L"Ag" : text.c_str();
    const int length = text.empty() ? 2 : static_cast<int>(text.size());
    GetTextExtentPoint32W(memory, measured, length, &size);
    if (text.empty()) size.cx = 0;
    SelectObject(memory, previous);
    DeleteDC(memory);
  }
  ReleaseDC(nullptr, screen);
  return size;
}

} // namespace

struct Context::Impl : std::enable_shared_from_this<Impl> {
  struct Node {
    Kind kind = Kind::window;
    HWND object = nullptr;
    HWND root = nullptr; // Window's automatic vertical container child.
    std::int64_t parent = 0, window = 0;
    std::string text;
    std::map<std::string, std::int64_t> properties;
    Placement placement;
    RECT allocation{0, 0, 0, 0};
    unsigned suppress = 0;
    bool event_failure = false;
  };
  std::thread::id owner = std::this_thread::get_id();
  std::map<std::int64_t, Node> nodes;
  std::map<HWND, std::int64_t> handles;
  std::map<std::int64_t, std::int64_t> focus_targets; // window handle -> widget handle
  std::deque<Event> events;
  std::map<std::int64_t, Event> snapshots;
  std::size_t event_bytes = 0, snapshot_bytes = 0, text_bytes = 0;
  std::atomic<bool> active{true};
  bool tearing_down = false;

  static std::mutex &deferred_mutex() { static auto *m = new std::mutex; return *m; }
  static std::vector<std::shared_ptr<Impl>> &deferred() {
    static auto *v = new std::vector<std::shared_ptr<Impl>>; return *v;
  }
  static void drain() {
    std::vector<std::shared_ptr<Impl>> pending;
    { std::lock_guard<std::mutex> lock(deferred_mutex()); pending.swap(deferred()); }
    for (auto &item : pending) item->dispose();
  }
  Result check() {
    if (owner != std::this_thread::get_id()) return failure("context must be used on its creation thread");
    if (!active.load()) return failure("context has been closed");
    auto &p = platform();
    bool initialized;
    {
      std::lock_guard<std::mutex> lock(p.thread_mutex);
      if (p.initialized && p.ui_thread != std::this_thread::get_id())
        return failure("Win32 windows must be used on the process UI thread");
      initialized = p.initialized;
    }
    if (initialized) drain();
    return success();
  }
  Result initialize() {
    if (auto result = check(); !result) return result;
    auto &p = platform();
    std::lock_guard<std::mutex> lock(p.thread_mutex);
    if (p.initialized) {
      if (p.ui_thread != std::this_thread::get_id())
        return failure("Win32 windows must be used on the process UI thread");
      return success();
    }
    if (!interactive_window_station())
      return failure("Win32 could not open a display; an interactive Windows window station is required (noninteractive service and SSH desktops are unsupported)");
    if (!p.registered) {
      WNDCLASSEXW description{};
      description.cbSize = sizeof description;
      description.lpfnWndProc = &window_proc;
      description.hInstance = GetModuleHandleW(nullptr);
      description.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
      description.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
      description.lpszClassName = window_class;
      if (!RegisterClassExW(&description) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return failure("Win32 could not register the window class");
      description.lpszClassName = container_class;
      if (!RegisterClassExW(&description) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return failure("Win32 could not register the container class");
      p.registered = true;
    }
    p.ui_thread = std::this_thread::get_id();
    p.initialized = true;
    return success();
  }
  Node *find(std::int64_t handle) {
    auto found = nodes.find(handle);
    return found == nodes.end() ? nullptr : &found->second;
  }
  static Result bad_handle() { return failure("unknown, closed, or foreign widget handle"); }

  HWND container_window(const Node &node) const { return node.kind == Kind::window ? node.root : node.object; }
  std::int64_t handle_of(HWND window) const {
    auto found = handles.find(window);
    return found == handles.end() ? 0 : found->second;
  }

  bool effective_sensitive(const Node &node) {
    if (!IsWindowEnabled(node.object)) return false;
    for (auto parent = node.parent; parent;) {
      auto *owner = find(parent);
      if (!owner) break;
      if (!IsWindowEnabled(owner->object)) return false;
      parent = owner->parent;
    }
    return true;
  }

  void enqueue(std::int64_t handle, std::int64_t kind) noexcept {
    const auto mark_failure = [&] {
      if (const auto *node = find(handle))
        if (auto *window = find(node->window)) window->event_failure = true;
    };
    try {
      auto *node = find(handle);
      if (!node || node->suppress || tearing_down) return;
      Event event{kind, handle, node->window, 0, {}};
      if (kind == 2) {
        event.text = window_text(node->object);
        if (!valid_text(event.text)) { mark_failure(); return; }
      } else if (kind == 3)
        event.checked = SendMessageW(node->object, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0;
      if (events.size() >= max_events || event.text.size() > max_event_text_bytes - event_bytes) {
        mark_failure(); return;
      }
      events.push_back(std::move(event));
      event_bytes += events.back().text.size();
    } catch (...) { mark_failure(); }
  }

  // ---- layout -------------------------------------------------------------
  // Win32 provides no natural size and no box/grid, so the provider computes
  // both. Children of a container are its nodes in handle order, which is
  // creation order because handles increase monotonically.
  std::vector<std::int64_t> children_of(std::int64_t handle) {
    std::vector<std::int64_t> result;
    for (const auto &[id, node] : nodes)
      if (node.parent == handle) result.push_back(id);
    return result;
  }
  std::int64_t property(const Node &node, const char *name, std::int64_t fallback = 0) {
    auto found = node.properties.find(name);
    if (found != node.properties.end()) return found->second;
    auto all = node.properties.find("margin");
    if (all != node.properties.end() && std::string(name).rfind("margin_", 0) == 0) return all->second;
    return fallback;
  }
  SIZE natural(std::int64_t handle) {
    auto *node = find(handle);
    if (!node) return SIZE{0, 0};
    SIZE size{0, 0};
    switch (node->kind) {
    case Kind::label: {
      const auto text = measure(to_wide(node->text));
      size = SIZE{text.cx + label_padding_x, text.cy + label_padding_y};
      break;
    }
    case Kind::button: {
      const auto text = measure(to_wide(node->text));
      size = SIZE{text.cx + button_padding_x, text.cy + button_padding_y};
      break;
    }
    case Kind::checkbox: {
      const auto text = measure(to_wide(node->text));
      size = SIZE{text.cx + checkbox_box_width + label_padding_x, text.cy + label_padding_y};
      break;
    }
    case Kind::entry: {
      const auto text = measure(to_wide(node->text));
      size = SIZE{(std::max)(static_cast<LONG>(entry_minimum_width), text.cx + entry_padding_x),
                  text.cy + entry_padding_y};
      break;
    }
    default: {
      const auto kids = children_of(handle);
      const auto spacing = static_cast<LONG>(property(*node, "spacing"));
      if (node->kind == Kind::grid) {
        std::map<std::int64_t, LONG> widths, heights;
        for (auto id : kids) {
          auto *child = find(id);
          if (!child) continue;
          const auto wanted = natural(id);
          auto &width = widths[child->placement.column];
          auto &height = heights[child->placement.row];
          width = (std::max)(width, static_cast<LONG>(wanted.cx / (std::max)(std::int64_t(1), child->placement.columns)));
          height = (std::max)(height, static_cast<LONG>(wanted.cy / (std::max)(std::int64_t(1), child->placement.rows)));
        }
        for (const auto &[column, width] : widths) { (void)column; size.cx += width; }
        for (const auto &[row, height] : heights) { (void)row; size.cy += height; }
        if (!widths.empty()) size.cx += spacing * static_cast<LONG>(widths.size() - 1);
        if (!heights.empty()) size.cy += spacing * static_cast<LONG>(heights.size() - 1);
      } else {
        const bool vertical = node->kind != Kind::row;
        for (auto id : kids) {
          const auto wanted = natural(id);
          if (vertical) { size.cx = (std::max)(size.cx, wanted.cx); size.cy += wanted.cy; }
          else { size.cy = (std::max)(size.cy, wanted.cy); size.cx += wanted.cx; }
        }
        if (!kids.empty()) {
          const auto gaps = spacing * static_cast<LONG>(kids.size() - 1);
          if (vertical) size.cy += gaps; else size.cx += gaps;
        }
      }
      break;
    }
    }
    if (node->kind != Kind::window) {
      size.cx += static_cast<LONG>(property(*node, "margin_start") + property(*node, "margin_end"));
      size.cy += static_cast<LONG>(property(*node, "margin_top") + property(*node, "margin_bottom"));
    }
    return size;
  }
  void place(std::int64_t handle, RECT area) {
    auto *node = find(handle);
    if (!node) return;
    if (node->kind != Kind::window) {
      area.left += static_cast<LONG>(property(*node, "margin_start"));
      area.right -= static_cast<LONG>(property(*node, "margin_end"));
      area.top += static_cast<LONG>(property(*node, "margin_top"));
      area.bottom -= static_cast<LONG>(property(*node, "margin_bottom"));
      if (area.right < area.left) area.right = area.left;
      if (area.bottom < area.top) area.bottom = area.top;
      node->allocation = area;
      SetWindowPos(node->object, nullptr, area.left, area.top, area.right - area.left,
                   area.bottom - area.top, SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
      node->allocation = area;
    }
    if (!container(node->kind)) return;

    // Containers lay out in their own client coordinates.
    RECT inner{0, 0, area.right - area.left, area.bottom - area.top};
    if (node->kind == Kind::window) {
      inner.left += static_cast<LONG>(property(*node, "margin_start"));
      inner.right -= static_cast<LONG>(property(*node, "margin_end"));
      inner.top += static_cast<LONG>(property(*node, "margin_top"));
      inner.bottom -= static_cast<LONG>(property(*node, "margin_bottom"));
      if (node->root)
        SetWindowPos(node->root, nullptr, 0, 0, area.right - area.left, area.bottom - area.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (inner.right < inner.left) inner.right = inner.left;
    if (inner.bottom < inner.top) inner.bottom = inner.top;

    const auto kids = children_of(handle);
    if (kids.empty()) return;
    const auto spacing = static_cast<LONG>(property(*node, "spacing"));

    if (node->kind == Kind::grid) {
      std::map<std::int64_t, LONG> widths, heights;
      for (auto id : kids) {
        auto *child = find(id);
        if (!child) continue;
        const auto wanted = natural(id);
        auto &width = widths[child->placement.column];
        auto &height = heights[child->placement.row];
        width = (std::max)(width, static_cast<LONG>(wanted.cx / (std::max)(std::int64_t(1), child->placement.columns)));
        height = (std::max)(height, static_cast<LONG>(wanted.cy / (std::max)(std::int64_t(1), child->placement.rows)));
      }
      std::map<std::int64_t, LONG> column_offset, row_offset;
      LONG x = inner.left;
      for (auto &[column, width] : widths) { column_offset[column] = x; x += width + spacing; }
      LONG y = inner.top;
      for (auto &[row, height] : heights) { row_offset[row] = y; y += height + spacing; }
      for (auto id : kids) {
        auto *child = find(id);
        if (!child) continue;
        LONG width = 0, height = 0;
        for (std::int64_t span = 0; span < child->placement.columns; ++span) {
          auto found = widths.find(child->placement.column + span);
          if (found != widths.end()) width += found->second + (span ? spacing : 0);
        }
        for (std::int64_t span = 0; span < child->placement.rows; ++span) {
          auto found = heights.find(child->placement.row + span);
          if (found != heights.end()) height += found->second + (span ? spacing : 0);
        }
        const LONG left = column_offset.count(child->placement.column) ? column_offset[child->placement.column] : inner.left;
        const LONG top = row_offset.count(child->placement.row) ? row_offset[child->placement.row] : inner.top;
        place(id, RECT{left, top, left + width, top + height});
      }
      return;
    }

    const bool vertical = node->kind != Kind::row;
    LONG used = spacing * static_cast<LONG>(kids.size() - 1);
    std::size_t expanding = 0;
    std::vector<SIZE> wanted;
    wanted.reserve(kids.size());
    for (auto id : kids) {
      wanted.push_back(natural(id));
      used += vertical ? wanted.back().cy : wanted.back().cx;
      auto *child = find(id);
      if (child && property(*child, vertical ? "vexpand" : "hexpand")) ++expanding;
    }
    const LONG span = vertical ? inner.bottom - inner.top : inner.right - inner.left;
    const LONG extra = span > used ? span - used : 0;
    const LONG share = expanding ? extra / static_cast<LONG>(expanding) : 0;

    LONG offset = vertical ? inner.top : inner.left;
    for (std::size_t index = 0; index < kids.size(); ++index) {
      auto *child = find(kids[index]);
      if (!child) continue;
      const bool expand = property(*child, vertical ? "vexpand" : "hexpand") != 0;
      LONG length = (vertical ? wanted[index].cy : wanted[index].cx) + (expand ? share : 0);
      RECT target;
      if (vertical) {
        const LONG width = property(*child, "hexpand") ? inner.right - inner.left
                                                       : (std::min)(wanted[index].cx, inner.right - inner.left);
        target = RECT{inner.left, offset, inner.left + width, offset + length};
      } else {
        const LONG height = property(*child, "vexpand") ? inner.bottom - inner.top
                                                        : (std::min)(wanted[index].cy, inner.bottom - inner.top);
        target = RECT{offset, inner.top, offset + length, inner.top + height};
      }
      place(kids[index], target);
      offset += length + spacing;
    }
  }
  void relayout(std::int64_t window) {
    auto *node = find(window);
    if (!node || node->kind != Kind::window) return;
    RECT client{};
    if (!GetClientRect(node->object, &client) || (client.right == 0 && client.bottom == 0))
      client = RECT{0, 0, static_cast<LONG>(property(*node, "width", 1)),
                    static_cast<LONG>(property(*node, "height", 1))};
    place(window, client);
  }

  // WM_COMMAND is delivered to a control's immediate parent, so windows and
  // containers share this procedure and each services its own children.
  // These are members for the same reason the GTK provider's callbacks are:
  // Context::Impl is private, so a free function cannot name it.
  static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM first, LPARAM second) {
    auto *impl = reinterpret_cast<Impl *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (impl && impl->active.load() && !impl->tearing_down) {
      switch (message) {
      case WM_COMMAND: {
        const auto control = reinterpret_cast<HWND>(second);
        const auto notification = HIWORD(first);
        const auto handle = control ? impl->handle_of(control) : 0;
        if (handle) {
          auto *node = impl->find(handle);
          if (node) {
            if (notification == BN_CLICKED && node->kind == Kind::button) impl->enqueue(handle, 1);
            else if (notification == BN_CLICKED && node->kind == Kind::checkbox) impl->enqueue(handle, 3);
            else if (notification == EN_CHANGE && node->kind == Kind::entry) impl->enqueue(handle, 2);
          }
        }
        return 0;
      }
      case WM_CLOSE: {
        // Matrix decides when to close; Win32 must not destroy a live handle.
        const auto handle = impl->handle_of(window);
        if (handle) { impl->enqueue(handle, 4); return 0; }
        break;
      }
      case WM_SIZE: {
        const auto handle = impl->handle_of(window);
        if (handle) {
          auto *node = impl->find(handle);
          // Only the top level relayouts. The root container maps to the same
          // handle, and laying out resizes it, so reacting here too would
          // re-enter relayout without end.
          if (node && node->kind == Kind::window && window == node->object) impl->relayout(handle);
        }
        break;
      }
      case WM_SETFOCUS: {
        const auto handle = impl->handle_of(window);
        if (handle) {
          auto *node = impl->find(handle);
          if (node && node->kind != Kind::window) impl->focus_targets[node->window] = handle;
        }
        break;
      }
      default: break;
      }
    }
    return DefWindowProcW(window, message, first, second);
  }

  // Controls are standard classes, so their notifications already reach the
  // parent; only focus tracking needs the control's own messages.
  static LRESULT CALLBACK control_proc(HWND control, UINT message, WPARAM first, LPARAM second) {
    const auto original = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(control, GWLP_USERDATA));
    if (message == WM_SETFOCUS) {
      const HWND parent = GetParent(control);
      if (parent)
        if (auto *impl = reinterpret_cast<Impl *>(GetWindowLongPtrW(parent, GWLP_USERDATA)))
          if (impl->active.load() && !impl->tearing_down) {
            const auto handle = impl->handle_of(control);
            if (handle)
              if (auto *node = impl->find(handle)) impl->focus_targets[node->window] = handle;
          }
    }
    return original ? CallWindowProcW(original, control, message, first, second)
                    : DefWindowProcW(control, message, first, second);
  }

  void dispose() noexcept {
    active.store(false);
    tearing_down = true;
    for (auto &[id, node] : nodes)
      if (node.kind == Kind::window && node.object) {
        SetWindowLongPtrW(node.object, GWLP_USERDATA, 0);
        DestroyWindow(node.object);
      }
    nodes.clear(); handles.clear(); focus_targets.clear();
    events.clear(); snapshots.clear();
    event_bytes = snapshot_bytes = text_bytes = 0;
    tearing_down = false;
  }
};

namespace {

bool keyboard_message(UINT message) {
  return message == WM_KEYDOWN || message == WM_KEYUP || message == WM_CHAR ||
         message == WM_SYSKEYDOWN || message == WM_SYSKEYUP || message == WM_SYSCHAR ||
         message == WM_DEADCHAR || message == WM_SYSDEADCHAR;
}

bool focusable(Kind kind) { return kind == Kind::button || kind == Kind::entry || kind == Kind::checkbox; }

} // namespace

// The Windows provider is part of the operating system, so unlike the optional
// GTK provider there is nothing to locate or verify. Reporting availability
// must still not open a display.
bool available() noexcept { return true; }

Context::Context() : impl_(std::make_shared<Impl>()) {}
Context::~Context() { clear(); }
void Context::clear() noexcept {
  if (!impl_) return;
  auto old = std::move(impl_);
  old->active.store(false);
  if (old->owner == std::this_thread::get_id()) old->dispose();
  else if (!old->nodes.empty()) {
    try { std::lock_guard<std::mutex> lock(Impl::deferred_mutex()); Impl::deferred().push_back(std::move(old)); }
    catch (...) { /* Windows owns the HWNDs; a failed defer may leak them but never frees context data twice. */ }
  }
  try { impl_ = std::make_shared<Impl>(); } catch (...) {}
}

Result Context::window(const std::string &title, std::int64_t width, std::int64_t height) {
  if (!impl_) return failure("context allocation failed");
  if (!valid_text(title)) return failure(text_error);
  if (width < 1 || width > 16384 || height < 1 || height > 16384)
    return failure("window width and height must be between 1 and 16384");
  if (auto result = impl_->initialize(); !result) return result;
  if (title.size() > max_event_text_bytes - impl_->text_bytes) return failure("stored text limit exceeded (16 MiB per context)");
  if (impl_->nodes.size() >= max_widgets || static_cast<std::size_t>(std::count_if(impl_->nodes.begin(), impl_->nodes.end(),
        [](const auto &item) { return item.second.kind == Kind::window; })) >= max_windows)
    return failure("window or widget limit exceeded (64 windows, 4096 widgets per context)");
  const auto handle = fresh_handle();
  if (!handle) return failure("handle space exhausted");
  Impl::Node node; node.kind = Kind::window; node.window = handle; node.text = title;
  node.properties = {{"width", width}, {"height", height}, {"spacing", 0}};

  // Creation remains hidden: WS_VISIBLE is set only by visible=1.
  RECT frame{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
  AdjustWindowRectEx(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0);
  node.object = CreateWindowExW(0, window_class, to_wide(title).c_str(), WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, frame.right - frame.left,
                                frame.bottom - frame.top, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
  if (!node.object) return failure("Win32 could not create a window");
  node.root = CreateWindowExW(WS_EX_CONTROLPARENT, container_class, L"",
                              WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                              0, 0, static_cast<int>(width), static_cast<int>(height), node.object,
                              nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!node.root) {
    DestroyWindow(node.object);
    return failure("Win32 could not create the root layout");
  }
  try {
    const HWND object = node.object, root = node.root;
    impl_->nodes.emplace(handle, std::move(node));
    impl_->handles.emplace(object, handle);
    impl_->handles.emplace(root, handle);
    SetWindowLongPtrW(object, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl_.get()));
    SetWindowLongPtrW(root, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl_.get()));
    impl_->text_bytes += title.size();
    impl_->relayout(handle);
  } catch (...) {
    auto found = impl_->nodes.find(handle);
    HWND object = found == impl_->nodes.end() ? node.object : found->second.object;
    if (found != impl_->nodes.end()) {
      impl_->handles.erase(found->second.object);
      impl_->handles.erase(found->second.root);
      impl_->nodes.erase(found);
    }
    if (object) { SetWindowLongPtrW(object, GWLP_USERDATA, 0); DestroyWindow(object); }
    return failure("window allocation failed");
  }
  return success(handle);
}

Result Context::widget(const std::string &kind, std::int64_t parent, const std::string &label) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  if (!valid_text(label)) return failure(text_error);
  if (label.size() > max_event_text_bytes - impl_->text_bytes) return failure("stored text limit exceeded (16 MiB per context)");
  if (kind == "entry" && !entry_text_fits(label)) return failure("entry text exceeds 16384 Unicode characters");
  auto *owner = impl_->find(parent);
  if (!owner) return Impl::bad_handle();
  if (!container(owner->kind)) return failure("parent must be a window, row, column, or grid");
  if (impl_->nodes.size() >= max_widgets) return failure("widget limit exceeded (4096 per context)");

  Impl::Node node; node.parent = parent; node.window = owner->window; node.text = label;
  const HWND host = impl_->container_window(*owner);
  const auto wide = to_wide(label);
  const DWORD common = WS_CHILD | WS_VISIBLE;
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  if (kind == "column" || kind == "row") {
    node.kind = kind == "column" ? Kind::column : Kind::row;
    node.object = CreateWindowExW(WS_EX_CONTROLPARENT, container_class, L"",
                                  common | WS_CLIPCHILDREN, 0, 0, 0, 0, host, nullptr, instance, nullptr);
  } else if (kind == "grid") {
    node.kind = Kind::grid;
    node.object = CreateWindowExW(WS_EX_CONTROLPARENT, container_class, L"",
                                  common | WS_CLIPCHILDREN, 0, 0, 0, 0, host, nullptr, instance, nullptr);
  } else if (kind == "label") {
    node.kind = Kind::label;
    node.object = CreateWindowExW(0, L"STATIC", wide.c_str(), common | SS_LEFT | SS_NOPREFIX, 0, 0, 0, 0, host, nullptr, instance, nullptr);
  } else if (kind == "button") {
    node.kind = Kind::button;
    node.object = CreateWindowExW(0, L"BUTTON", wide.c_str(), common | BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 0, 0, host, nullptr, instance, nullptr);
  } else if (kind == "entry") {
    node.kind = Kind::entry;
    node.object = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", wide.c_str(),
                                  common | ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP, 0, 0, 0, 0, host, nullptr, instance, nullptr);
  } else if (kind == "checkbox") {
    node.kind = Kind::checkbox;
    node.object = CreateWindowExW(0, L"BUTTON", wide.c_str(), common | BS_AUTOCHECKBOX | WS_TABSTOP, 0, 0, 0, 0, host, nullptr, instance, nullptr);
  } else return failure("widget kind must be column, row, grid, label, button, entry, or checkbox");
  if (!node.object) return failure("Win32 could not create the widget");

  const auto handle = fresh_handle();
  if (!handle) { DestroyWindow(node.object); return failure("handle space exhausted"); }
  try {
    SendMessageW(node.object, WM_SETFONT, reinterpret_cast<WPARAM>(platform().message_font()), TRUE);
    if (node.kind == Kind::entry) SendMessageW(node.object, EM_SETLIMITTEXT, max_entry_characters, 0);
    if (owner->kind == Kind::grid) {
      std::vector<std::pair<std::int64_t, std::int64_t>> occupied;
      for (const auto &[id, sibling] : impl_->nodes)
        if (sibling.parent == parent && sibling.placement.column == 0)
          occupied.emplace_back(sibling.placement.row, sibling.placement.row + sibling.placement.rows);
      std::sort(occupied.begin(), occupied.end());
      for (const auto &[start, end] : occupied) {
        if (node.placement.row < start) break;
        node.placement.row = (std::max)(node.placement.row, end);
      }
      if (node.placement.row > 65535) { DestroyWindow(node.object); return failure("grid row limit exceeded"); }
    }
    const HWND object = node.object;
    const bool track_focus = focusable(node.kind);
    const bool is_container = container(node.kind);
    impl_->nodes.emplace(handle, std::move(node));
    impl_->handles.emplace(object, handle);
    // A control's WM_COMMAND is delivered to its immediate parent, which for
    // anything inside a row, column or grid is that container rather than the
    // window. Without the context here those notifications are dropped.
    if (is_container) SetWindowLongPtrW(object, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl_.get()));
    if (track_focus) {
      // Keep the original procedure in the control's user data; the provider
      // only needs WM_SETFOCUS, and every other message stays untouched.
      const auto original = GetWindowLongPtrW(object, GWLP_WNDPROC);
      SetWindowLongPtrW(object, GWLP_USERDATA, original);
      SetWindowLongPtrW(object, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&Impl::control_proc));
    }
    impl_->text_bytes += label.size();
    impl_->relayout(impl_->nodes.at(handle).window);
  } catch (...) {
    auto found = impl_->nodes.find(handle);
    if (found != impl_->nodes.end()) {
      impl_->handles.erase(found->second.object);
      DestroyWindow(found->second.object);
      impl_->nodes.erase(found);
    } else DestroyWindow(node.object);
    return failure("widget allocation failed");
  }
  return success(handle);
}

Result Context::grid_attach(std::int64_t grid, std::int64_t child, std::int64_t column,
                            std::int64_t row, std::int64_t column_span, std::int64_t row_span) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  auto *parent = impl_->find(grid); auto *node = impl_->find(child);
  if (!parent || !node) return Impl::bad_handle();
  if (parent->kind != Kind::grid || node->parent != grid)
    return failure("grid_attach requires a child of the specified grid");
  if (column < 0 || row < 0 || column_span < 1 || row_span < 1 ||
      column > 65535 || row > 65535 || column_span > 65536 - column || row_span > 65536 - row)
    return failure("grid coordinates and spans must fit within 0..65535");
  Placement placement{column, row, column_span, row_span};
  for (const auto &[id, other] : impl_->nodes)
    if (id != child && other.parent == grid && overlaps(placement, other.placement))
      return failure("grid placement overlaps an existing child");
  node->placement = placement;
  impl_->relayout(node->window);
  return success();
}

Result Context::set_text(std::int64_t handle, const std::string &text) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  if (!valid_text(text)) return failure(text_error);
  auto *node = impl_->find(handle); if (!node) return Impl::bad_handle();
  if (container(node->kind) && node->kind != Kind::window) return failure("container does not have a text property");
  if (node->kind == Kind::entry && !entry_text_fits(text)) return failure("entry text exceeds 16384 Unicode characters");
  if (text.size() > max_event_text_bytes - (impl_->text_bytes - node->text.size())) return failure("stored text limit exceeded (16 MiB per context)");
  std::string copy = text;
  const auto wide = to_wide(copy);
  // Programmatic writes must not raise a change event.
  ++node->suppress;
  const BOOL changed = SetWindowTextW(node->object, wide.c_str());
  --node->suppress;
  if (!changed) return failure("Win32 could not set the widget text");
  impl_->text_bytes = impl_->text_bytes - node->text.size() + copy.size();
  node->text = std::move(copy);
  if (node->kind != Kind::window) impl_->relayout(node->window);
  return success();
}

Result Context::read_text(std::int64_t handle) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  auto *node = impl_->find(handle); if (!node) return Impl::bad_handle();
  if (container(node->kind) && node->kind != Kind::window) return failure("container does not have a text property");
  Result result;
  if (node->kind == Kind::entry) {
    const int length = GetWindowTextLengthW(node->object);
    std::wstring wide(static_cast<std::size_t>(length < 0 ? 0 : length) + 1, L'\0');
    const int copied = GetWindowTextW(node->object, wide.data(), length + 1);
    wide.resize(copied < 0 ? 0 : static_cast<std::size_t>(copied));
    if (!to_utf8(wide, result.text)) return failure("Win32 returned invalid text");
  } else result.text = node->text;
  if (!valid_text(result.text)) return failure(text_error);
  return result;
}

Result Context::set_number(std::int64_t handle, const std::string &property, std::int64_t value) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  auto *node = impl_->find(handle); if (!node) return Impl::bad_handle();
  if (property == "focus") {
    if (value != 1) return failure("focus accepts only 1 to request keyboard focus");
    if (!focusable(node->kind)) return failure("widget cannot receive keyboard focus");
    if (!impl_->effective_sensitive(*node)) return failure("widget cannot receive keyboard focus");
    impl_->focus_targets[node->window] = handle;
    SetFocus(node->object);
    return success();
  }
  const bool flag = property == "visible" || property == "sensitive" || property == "hexpand" || property == "vexpand" || property == "checked";
  const bool margin = property == "margin" || property == "margin_start" || property == "margin_end" || property == "margin_top" || property == "margin_bottom";
  const bool size = property == "width" || property == "height";
  if (!flag && !margin && !size && property != "spacing") return failure("unknown or read-only numeric property");
  if (flag && value != 0 && value != 1) return failure("flag properties require exactly 0 or 1");
  if ((margin || property == "spacing") && (value < 0 || value > 4096)) return failure("margins and spacing must be between 0 and 4096");
  if (size && (node->kind != Kind::window || value < 1 || value > 16384)) return failure("width and height are window properties between 1 and 16384");
  if (property == "checked" && node->kind != Kind::checkbox) return failure("checked requires a checkbox");
  if (property == "spacing" && !container(node->kind)) return failure("spacing requires a window, row, column, or grid");
  node->properties[property] = value;
  if (property == "margin")
    for (const auto *side : {"margin_start", "margin_end", "margin_top", "margin_bottom"}) node->properties[side] = value;

  if (property == "visible") {
    ShowWindow(node->object, value ? SW_SHOW : SW_HIDE);
    if (value && node->kind == Kind::window) {
      SetForegroundWindow(node->object);
      impl_->relayout(handle);
    } else impl_->relayout(node->window);
  } else if (property == "sensitive") {
    EnableWindow(node->object, value ? TRUE : FALSE);
  } else if (property == "checked") {
    ++node->suppress;
    SendMessageW(node->object, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    --node->suppress;
  } else if (size) {
    RECT frame{0, 0, static_cast<LONG>(node->properties.at("width")), static_cast<LONG>(node->properties.at("height"))};
    AdjustWindowRectEx(&frame, static_cast<DWORD>(GetWindowLongPtrW(node->object, GWL_STYLE)), FALSE, 0);
    SetWindowPos(node->object, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    impl_->relayout(handle);
  } else impl_->relayout(node->window);
  return success();
}

Result Context::read_number(std::int64_t handle, const std::string &property) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  auto *node = impl_->find(handle); if (!node) return Impl::bad_handle();
  if (property == "kind") return success(static_cast<std::int64_t>(node->kind));
  if (property == "parent") return success(node->parent);
  if (property == "checked") {
    if (node->kind != Kind::checkbox) return failure("checked requires a checkbox");
    return success(SendMessageW(node->object, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0);
  }
  if (property == "visible")
    return success((GetWindowLongPtrW(node->object, GWL_STYLE) & WS_VISIBLE) ? 1 : 0);
  if (property == "effective_visible") return success(IsWindowVisible(node->object) ? 1 : 0);
  if (property == "sensitive") return success(IsWindowEnabled(node->object) ? 1 : 0);
  if (property == "effective_sensitive") return success(impl_->effective_sensitive(*node) ? 1 : 0);
  if (property == "focus") {
    auto found = impl_->focus_targets.find(node->window);
    if (found == impl_->focus_targets.end()) return success(0);
    if (found->second == handle) return success(1);
    auto *target = impl_->find(found->second);
    // A compound widget's internal child counts as the widget's own focus.
    return success(target && node->object && IsChild(node->object, target->object) ? 1 : 0);
  }
  if (property == "width" || property == "height") {
    if (node->kind == Kind::window) {
      RECT client{};
      if (!IsWindowVisible(node->object) || !GetClientRect(node->object, &client) ||
          (client.right == 0 && client.bottom == 0))
        return success(node->properties.at(property));
      return success(property == "width" ? client.right - client.left : client.bottom - client.top);
    }
    return success(property == "width" ? node->allocation.right - node->allocation.left
                                       : node->allocation.bottom - node->allocation.top);
  }
  if (property == "spacing" && !container(node->kind)) return failure("spacing requires a window, row, column, or grid");
  if (property != "spacing" && property != "hexpand" && property != "vexpand" && property != "margin" &&
      property != "margin_start" && property != "margin_end" && property != "margin_top" && property != "margin_bottom")
    return failure("unknown numeric property");
  auto found = node->properties.find(property);
  if (property == "margin") {
    const auto side_value = [&](const char *side) {
      auto item = node->properties.find(side); return item == node->properties.end() ? std::int64_t(0) : item->second;
    };
    const auto start = side_value("margin_start");
    if (start != side_value("margin_end") || start != side_value("margin_top") || start != side_value("margin_bottom"))
      return failure("margins differ; read each margin side separately");
    return success(start);
  }
  if (found != node->properties.end()) return success(found->second);
  if (property.rfind("margin_", 0) == 0) {
    auto all = node->properties.find("margin"); if (all != node->properties.end()) return success(all->second);
  }
  return success(0);
}

Result Context::activate(std::int64_t handle) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  auto *node = impl_->find(handle); if (!node) return Impl::bad_handle();
  if (node->kind != Kind::button && node->kind != Kind::checkbox) return failure("activate requires a button or checkbox");
  if (!impl_->effective_sensitive(*node))
    return failure("cannot activate an insensitive widget or a child of an insensitive container");
  // BM_CLICK is the real activation path: it toggles an auto checkbox and
  // notifies the parent exactly as a user click does.
  SendMessageW(node->object, BM_CLICK, 0, 0);
  return success();
}

Result Context::poll(std::int64_t window) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  auto *node = impl_->find(window); if (!node) return Impl::bad_handle();
  if (node->kind != Kind::window) return failure("poll requires a window");
  MSG message;
  for (unsigned count = 0; count < 128; ++count) {
    if (!PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) break;
    // Real keystrokes are delivered by the system straight to the focused
    // control, so that is what the dialog manager and the control each expect
    // to see. Input addressed to the top level instead -- which is how a
    // caller drives a window it does not type into -- is retargeted here to
    // the focused control, because GTK routes such input through the toplevel
    // and the contract is written against that behaviour.
    if (keyboard_message(message.message) && message.hwnd == node->object) {
      const auto target = impl_->focus_targets.find(window);
      if (target != impl_->focus_targets.end())
        if (auto *focused = impl_->find(target->second); focused && focused->object)
          message.hwnd = focused->object;
    }
    // Tab order, arrow groups and the activations that go with them are the
    // dialog manager's work on Win32, where GTK does it inside the toolkit.
    if (IsDialogMessageW(node->object, &message)) continue;
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  if (node->event_failure) {
    node->event_failure = false;
    return failure("event queue limit or allocation exceeded; one or more events were lost");
  }
  auto found = std::find_if(impl_->events.begin(), impl_->events.end(),
                            [window](const auto &event) { return event.window == window; });
  if (found == impl_->events.end()) return success(0);
  if (impl_->snapshots.size() >= max_snapshots || found->text.size() > max_event_text_bytes - impl_->snapshot_bytes)
    return failure("event snapshot limit exceeded; release previously returned snapshots");
  auto handle = fresh_handle(); if (!handle) return failure("handle space exhausted");
  impl_->snapshots.emplace(handle, *found);
  impl_->snapshot_bytes += found->text.size(); impl_->event_bytes -= found->text.size();
  impl_->events.erase(found);
  return success(handle);
}

Result Context::event_number(std::int64_t snapshot, const std::string &field) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  auto found = impl_->snapshots.find(snapshot);
  if (found == impl_->snapshots.end()) return failure("unknown, released, or foreign event snapshot");
  const auto &event = found->second;
  if (field == "kind") return success(event.kind);
  if (field == "target") return success(event.target);
  if (field == "window") return success(event.window);
  if (field == "checked") return success(event.checked);
  return failure("event field must be kind, target, window, or checked");
}

Result Context::event_text(std::int64_t snapshot) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  auto found = impl_->snapshots.find(snapshot);
  if (found == impl_->snapshots.end()) return failure("unknown, released, or foreign event snapshot");
  return {0, found->second.text, {}};
}

Result Context::release_event(std::int64_t snapshot) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  auto found = impl_->snapshots.find(snapshot);
  if (found == impl_->snapshots.end()) return failure("unknown, released, or foreign event snapshot");
  impl_->snapshot_bytes -= found->second.text.size(); impl_->snapshots.erase(found);
  return success();
}

Result Context::destroy_widget(std::int64_t handle) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  auto *node = impl_->find(handle); if (!node) return Impl::bad_handle();
  if (node->kind == Kind::window)
    return failure("destroy_widget requires a non-window widget; use close for windows");
  auto *parent = impl_->find(node->parent);
  if (!parent || !container(parent->kind)) return failure("widget parent is no longer a live container");

  // Handles increase monotonically and children are created after their
  // parent, so one ordered pass collects the complete subtree.
  std::vector<std::int64_t> removed;
  try {
    removed.reserve(impl_->nodes.size());
    for (const auto &[id, child] : impl_->nodes)
      if (id == handle || std::binary_search(removed.begin(), removed.end(), child.parent))
        removed.push_back(id);
  } catch (...) { return failure("widget removal allocation failed"); }

  const auto window = node->window;
  const HWND root = node->object;
  // DestroyWindow takes the descendants with it; ignore their notifications.
  impl_->tearing_down = true;
  for (auto id : removed)
    if (auto *child = impl_->find(id)) impl_->handles.erase(child->object);
  DestroyWindow(root);
  impl_->tearing_down = false;

  for (auto it = removed.rbegin(); it != removed.rend(); ++it) {
    auto found = impl_->nodes.find(*it);
    if (found == impl_->nodes.end()) continue;
    impl_->text_bytes -= found->second.text.size();
    impl_->nodes.erase(found);
  }
  const auto targeted = [&](std::int64_t target) { return std::binary_search(removed.begin(), removed.end(), target); };
  for (auto it = impl_->events.begin(); it != impl_->events.end();) {
    if (!targeted(it->target)) { ++it; continue; }
    impl_->event_bytes -= it->text.size(); it = impl_->events.erase(it);
  }
  for (auto it = impl_->snapshots.begin(); it != impl_->snapshots.end();) {
    if (!targeted(it->second.target)) { ++it; continue; }
    impl_->snapshot_bytes -= it->second.text.size(); it = impl_->snapshots.erase(it);
  }
  auto focus = impl_->focus_targets.find(window);
  if (focus != impl_->focus_targets.end() && targeted(focus->second)) impl_->focus_targets.erase(focus);
  impl_->relayout(window);
  return success();
}

Result Context::close(std::int64_t window) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  auto *node = impl_->find(window); if (!node) return Impl::bad_handle();
  if (node->kind != Kind::window) return failure("close requires a window");

  const HWND object = node->object;
  impl_->tearing_down = true;
  for (const auto &[id, child] : impl_->nodes)
    if (child.window == window) {
      impl_->handles.erase(child.object);
      if (child.root) impl_->handles.erase(child.root);
    }
  SetWindowLongPtrW(object, GWLP_USERDATA, 0);
  DestroyWindow(object);
  impl_->tearing_down = false;

  for (auto it = impl_->nodes.begin(); it != impl_->nodes.end();) {
    if (it->second.window != window) { ++it; continue; }
    impl_->text_bytes -= it->second.text.size();
    it = impl_->nodes.erase(it);
  }
  for (auto it = impl_->events.begin(); it != impl_->events.end();) {
    if (it->window != window) { ++it; continue; }
    impl_->event_bytes -= it->text.size(); it = impl_->events.erase(it);
  }
  for (auto it = impl_->snapshots.begin(); it != impl_->snapshots.end();) {
    if (it->second.window != window) { ++it; continue; }
    impl_->snapshot_bytes -= it->second.text.size(); it = impl_->snapshots.erase(it);
  }
  impl_->focus_targets.erase(window);
  return success();
}
} // namespace ny::ui
