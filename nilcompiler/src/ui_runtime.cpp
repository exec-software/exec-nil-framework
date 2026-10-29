/*
 * Copyright (C) 2008 - 2026 Execute Software. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#include "ui_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
#if defined(__linux__)
#include <dlfcn.h>
#endif

namespace ny::ui {
namespace {
// This optional loader uses only GTK 4's public opaque-pointer C ABI. It does
// not reproduce toolkit object layouts or depend on the legacy generic FFI.
struct GtkWidget; struct GtkWindow; struct GtkBox; struct GtkGrid;
struct GtkLabel; struct GtkButton; struct GtkEditable; struct GtkCheckButton; struct GtkEntry;
struct GObject; struct GClosure; struct GMainContext;
enum GtkOrientation { horizontal = 0, vertical = 1 };
using Callback = void (*)();
using ClosureNotify = void (*)(void *, GClosure *);
enum ConnectFlags { connect_default = 0 };

struct Api {
  bool loaded = false;
  std::mutex thread_mutex;
  bool initialized = false;
  std::thread::id ui_thread;
#define UI_SYMBOLS(X) \
  X(int, gtk_init_check, ()) \
  X(unsigned, gtk_get_major_version, ()) \
  X(GtkWidget *, gtk_window_new, ()) \
  X(void, gtk_window_set_title, (GtkWindow *, const char *)) \
  X(void, gtk_window_set_default_size, (GtkWindow *, int, int)) \
  X(void, gtk_window_set_child, (GtkWindow *, GtkWidget *)) \
  X(void, gtk_window_present, (GtkWindow *)) \
  X(void, gtk_window_destroy, (GtkWindow *)) \
  X(GtkWidget *, gtk_box_new, (GtkOrientation, int)) \
  X(void, gtk_box_append, (GtkBox *, GtkWidget *)) \
  X(void, gtk_box_set_spacing, (GtkBox *, int)) \
  X(GtkWidget *, gtk_grid_new, ()) \
  X(void, gtk_grid_attach, (GtkGrid *, GtkWidget *, int, int, int, int)) \
  X(void, gtk_grid_remove, (GtkGrid *, GtkWidget *)) \
  X(void, gtk_grid_set_row_spacing, (GtkGrid *, unsigned)) \
  X(void, gtk_grid_set_column_spacing, (GtkGrid *, unsigned)) \
  X(GtkWidget *, gtk_label_new, (const char *)) \
  X(void, gtk_label_set_text, (GtkLabel *, const char *)) \
  X(GtkWidget *, gtk_button_new_with_label, (const char *)) \
  X(void, gtk_button_set_label, (GtkButton *, const char *)) \
  X(GtkWidget *, gtk_entry_new, ()) \
  X(void, gtk_entry_set_max_length, (GtkEntry *, int)) \
  X(const char *, gtk_editable_get_text, (GtkEditable *)) \
  X(void, gtk_editable_set_text, (GtkEditable *, const char *)) \
  X(GtkWidget *, gtk_check_button_new_with_label, (const char *)) \
  X(int, gtk_check_button_get_active, (GtkCheckButton *)) \
  X(void, gtk_check_button_set_active, (GtkCheckButton *, int)) \
  X(void, gtk_check_button_set_label, (GtkCheckButton *, const char *)) \
  X(int, gtk_widget_activate, (GtkWidget *)) \
  X(void, gtk_widget_set_visible, (GtkWidget *, int)) \
  X(int, gtk_widget_get_visible, (GtkWidget *)) \
  X(void, gtk_widget_set_sensitive, (GtkWidget *, int)) \
  X(int, gtk_widget_get_sensitive, (GtkWidget *)) \
  X(int, gtk_widget_is_sensitive, (GtkWidget *)) \
  X(int, gtk_widget_grab_focus, (GtkWidget *)) \
  X(int, gtk_widget_has_focus, (GtkWidget *)) \
  X(void, gtk_widget_set_hexpand, (GtkWidget *, int)) \
  X(void, gtk_widget_set_vexpand, (GtkWidget *, int)) \
  X(void, gtk_widget_set_margin_start, (GtkWidget *, int)) \
  X(void, gtk_widget_set_margin_end, (GtkWidget *, int)) \
  X(void, gtk_widget_set_margin_top, (GtkWidget *, int)) \
  X(void, gtk_widget_set_margin_bottom, (GtkWidget *, int)) \
  X(int, gtk_widget_get_width, (GtkWidget *)) \
  X(int, gtk_widget_get_height, (GtkWidget *)) \
  X(int, gtk_widget_get_realized, (GtkWidget *)) \
  X(unsigned long, g_signal_connect_data, (void *, const char *, Callback, void *, ClosureNotify, ConnectFlags)) \
  X(void, g_signal_handler_disconnect, (void *, unsigned long)) \
  X(void *, g_object_ref, (void *)) \
  X(void *, g_object_ref_sink, (void *)) \
  X(void, g_object_unref, (void *)) \
  X(int, g_main_context_iteration, (GMainContext *, int))
#define DECLARE_SYMBOL(result, name, arguments) result (*name) arguments = nullptr;
  UI_SYMBOLS(DECLARE_SYMBOL)
#undef DECLARE_SYMBOL
  Api() noexcept {
#if defined(__linux__)
    // Keep these libraries loaded: GTK owns process-wide types, callbacks and
    // caches which must not outlive their code. No display is opened here.
    void *gtk = dlopen("libgtk-4.so.1", RTLD_NOW | RTLD_LOCAL);
    void *object = dlopen("libgobject-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
    void *glib = dlopen("libglib-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!gtk || !object || !glib) return;
#define LOAD_SYMBOL(result, name, arguments) \
    name = reinterpret_cast<decltype(name)>(dlsym(gtk, #name)); \
    if (!name) name = reinterpret_cast<decltype(name)>(dlsym(object, #name)); \
    if (!name) name = reinterpret_cast<decltype(name)>(dlsym(glib, #name)); \
    if (!name) return;
    UI_SYMBOLS(LOAD_SYMBOL)
#undef LOAD_SYMBOL
    loaded = gtk_get_major_version() == 4;
#endif
  }
#undef UI_SYMBOLS
};

Api &api() { static Api *value = new Api; return *value; }
constexpr std::size_t max_widgets = 4096, max_windows = 64;
constexpr std::size_t max_events = 4096, max_snapshots = 4096;
constexpr std::size_t max_text_bytes = 1024 * 1024;
constexpr std::size_t max_event_text_bytes = 16 * 1024 * 1024;
constexpr std::size_t max_entry_characters = 16384;
std::atomic<std::int64_t> next_handle{1};
std::int64_t fresh_handle() {
  auto value = next_handle.load(std::memory_order_relaxed);
  for (;;) {
    if (value <= 0 || value == std::numeric_limits<std::int64_t>::max()) return 0;
    if (next_handle.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) return value;
  }
}
Result failure(const std::string &message) { return {0, {}, "ui: " + message}; }
Result success(std::int64_t value = 1) { return {value, {}, {}}; }

bool valid_text(const std::string &text) {
  if (text.size() > max_text_bytes) return false;
  // Validate scalar-value UTF-8, reject embedded NUL and overlong encodings.
  for (std::size_t index = 0; index < text.size();) {
    const auto first = static_cast<unsigned char>(text[index++]);
    if (!first) return false;
    if (first < 0x80) continue;
    unsigned count, scalar, minimum;
    if (first >= 0xc2 && first <= 0xdf) { count = 1; scalar = first & 31; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { count = 2; scalar = first & 15; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { count = 3; scalar = first & 7; minimum = 0x10000; }
    else return false;
    if (count > text.size() - index) return false;
    while (count--) {
      auto byte = static_cast<unsigned char>(text[index++]);
      if ((byte & 0xc0) != 0x80) return false;
      scalar = (scalar << 6) | (byte & 63);
    }
    if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) return false;
  }
  return true;
}
const char *text_error = "text must be valid UTF-8 without NUL, at most 1048576 bytes";
bool entry_text_fits(const std::string &text) {
  return static_cast<std::size_t>(std::count_if(text.begin(), text.end(),
      [](unsigned char byte) { return (byte & 0xc0) != 0x80; })) <= max_entry_characters;
}
enum class Kind { window = 0, column = 1, row = 2, grid = 3, label = 4, button = 5, entry = 6, checkbox = 7 };
bool container(Kind kind) { return kind == Kind::window || kind == Kind::column || kind == Kind::row || kind == Kind::grid; }
struct Event { std::int64_t kind, target, window, checked; std::string text; };
struct Placement { std::int64_t column = 0, row = 0, columns = 1, rows = 1; };
bool overlaps(const Placement &a, const Placement &b) {
  return a.column < b.column + b.columns && b.column < a.column + a.columns &&
         a.row < b.row + b.rows && b.row < a.row + a.rows;
}
} // namespace

struct Context::Impl : std::enable_shared_from_this<Impl> {
  struct Node {
    Kind kind;
    GtkWidget *object = nullptr;
    GtkWidget *root = nullptr; // Window's auto-column, owned by GTK's parent tree.
    std::int64_t parent = 0, window = 0;
    std::string text;
    std::map<std::string, std::int64_t> properties;
    std::vector<unsigned long> signals;
    Placement placement;
    unsigned suppress = 0;
  };
  struct SignalData { std::weak_ptr<Impl> context; std::int64_t handle; std::int64_t kind; };
  std::thread::id owner = std::this_thread::get_id();
  std::map<std::int64_t, Node> nodes;
  std::deque<Event> events;
  std::map<std::int64_t, Event> snapshots;
  std::size_t event_bytes = 0, snapshot_bytes = 0;
  std::size_t text_bytes = 0;
  bool event_failure = false;
  std::atomic<bool> active{true};

  // Deferred teardown keeps GTK calls on its UI thread even when an embedding
  // host destroys a Context elsewhere. Each callback also holds only a weak
  // context reference, so no callback can dereference a destroyed Context.
  static std::mutex &deferred_mutex() { static auto *m = new std::mutex; return *m; }
  static std::vector<std::shared_ptr<Impl>> &deferred() { static auto *v = new std::vector<std::shared_ptr<Impl>>; return *v; }
  static void drain() {
    std::vector<std::shared_ptr<Impl>> pending;
    { std::lock_guard<std::mutex> lock(deferred_mutex()); pending.swap(deferred()); }
    for (auto &item : pending) item->dispose();
  }
  Result check() {
    if (owner != std::this_thread::get_id()) return failure("context must be used on its creation thread");
    if (!active.load()) return failure("context has been closed");
    auto &a = api();
    bool initialized;
    {
      std::lock_guard<std::mutex> lock(a.thread_mutex);
      if (a.initialized && a.ui_thread != std::this_thread::get_id())
        return failure("GTK must be used on the process UI thread");
      initialized = a.initialized;
    }
    if (initialized) drain();
    return success();
  }
  Result initialize() {
    if (auto result = check(); !result) return result;
    auto &a = api();
    if (!a.loaded) return failure("GTK 4 provider is unavailable; install the GTK 4 runtime");
    std::lock_guard<std::mutex> lock(a.thread_mutex);
    // A different context may have initialized GTK between check() and this
    // lock. Never allow that race to create a widget on the losing thread.
    if (a.initialized) {
      if (a.ui_thread != std::this_thread::get_id())
        return failure("GTK must be used on the process UI thread");
      return success();
    }
    if (!a.gtk_init_check()) return failure("GTK could not open a display; a working Wayland or X11 session is required");
    a.ui_thread = std::this_thread::get_id();
    a.initialized = true;
    return success();
  }
  Node *find(std::int64_t handle) {
    auto found = nodes.find(handle);
    return found == nodes.end() ? nullptr : &found->second;
  }
  static Result bad_handle() { return failure("unknown, closed, or foreign widget handle"); }
  static void destroy_signal(void *raw, GClosure *) noexcept { delete static_cast<SignalData *>(raw); }
  static void notify(void *, void *raw) noexcept {
    auto *data = static_cast<SignalData *>(raw);
    if (auto context = data->context.lock(); context && context->active.load()) context->enqueue(data->handle, data->kind);
  }
  static int close_requested(GtkWindow *, void *raw) noexcept {
    notify(nullptr, raw);
    // Matrix decides when to close. GTK must not destroy a still-live handle.
    return 1;
  }
  void connect(std::int64_t handle, const char *name, std::int64_t kind, bool closing = false) {
    auto &node = nodes.at(handle);
    auto data = std::make_unique<SignalData>(SignalData{weak_from_this(), handle, kind});
    // Reserve before GTK takes ownership, so allocation failure cannot leave an
    // untracked live signal attached to a widget being rolled back.
    node.signals.reserve(node.signals.size() + 1);
    auto callback = closing ? reinterpret_cast<Callback>(&close_requested) : reinterpret_cast<Callback>(&notify);
    auto signal = api().g_signal_connect_data(node.object, name, callback, data.get(), &destroy_signal, connect_default);
    data.release();
    node.signals.push_back(signal);
  }
  void enqueue(std::int64_t handle, std::int64_t kind) noexcept {
    try {
      auto *node = find(handle);
      if (!node || node->suppress) return;
      Event event{kind, handle, node->window, 0, {}};
      if (kind == 2) {
        const char *value = api().gtk_editable_get_text(reinterpret_cast<GtkEditable *>(node->object));
        if (!value) { event_failure = true; return; }
        event.text = value;
        if (!valid_text(event.text)) { event_failure = true; return; }
      } else if (kind == 3)
        event.checked = api().gtk_check_button_get_active(reinterpret_cast<GtkCheckButton *>(node->object)) ? 1 : 0;
      if (events.size() >= max_events || event.text.size() > max_event_text_bytes - event_bytes) {
        event_failure = true; return;
      }
      events.push_back(std::move(event));
      event_bytes += events.back().text.size();
    } catch (...) { event_failure = true; }
  }
  void disconnect(Node &node) noexcept {
    for (auto signal : node.signals) api().g_signal_handler_disconnect(node.object, signal);
    node.signals.clear();
  }
  void dispose() noexcept {
    active.store(false);
    // Normal VM/native invocations also clear their unused UI context. Keep
    // the optional toolkit completely unloaded until a UI operation needs it.
    if (!nodes.empty()) {
      auto &a = api();
      for (auto &[id, node] : nodes) disconnect(node);
      for (auto &[id, node] : nodes) if (node.kind == Kind::window)
        a.gtk_window_destroy(reinterpret_cast<GtkWindow *>(node.object));
      for (auto &[id, node] : nodes) a.g_object_unref(node.object);
    }
    nodes.clear(); events.clear(); snapshots.clear(); event_bytes = snapshot_bytes = text_bytes = 0;
  }
};

bool available() noexcept { try { return api().loaded; } catch (...) { return false; } }
Context::Context() : impl_(std::make_shared<Impl>()) {}
Context::~Context() { clear(); }
void Context::clear() noexcept {
  if (!impl_) return;
  auto old = std::move(impl_);
  old->active.store(false);
  if (old->owner == std::this_thread::get_id()) old->dispose();
  else if (!old->nodes.empty()) {
    try { std::lock_guard<std::mutex> lock(Impl::deferred_mutex()); Impl::deferred().push_back(std::move(old)); }
    catch (...) { /* Callbacks retain weak references; an OOM may leak GTK objects but cannot use freed context data. */ }
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
  if (impl_->nodes.size() >= max_widgets || std::count_if(impl_->nodes.begin(), impl_->nodes.end(),
        [](const auto &item) { return item.second.kind == Kind::window; }) >= max_windows)
    return failure("window or widget limit exceeded (64 windows, 4096 widgets per context)");
  const auto handle = fresh_handle();
  if (!handle) return failure("handle space exhausted");
  Impl::Node node; node.kind = Kind::window; node.window = handle; node.text = title;
  node.properties = {{"width", width}, {"height", height}, {"spacing", 0}};
  auto &a = api();
  node.object = a.gtk_window_new();
  if (!node.object) return failure("GTK could not create a window");
  a.g_object_ref(node.object); // gtk_window_new returns GTK's reference.
  try {
    node.root = a.gtk_box_new(vertical, 0);
    if (!node.root) throw std::runtime_error("GTK could not create the root layout");
    a.gtk_window_set_child(reinterpret_cast<GtkWindow *>(node.object), node.root);
    a.gtk_window_set_title(reinterpret_cast<GtkWindow *>(node.object), title.c_str());
    a.gtk_window_set_default_size(reinterpret_cast<GtkWindow *>(node.object), static_cast<int>(width), static_cast<int>(height));
    impl_->nodes.emplace(handle, std::move(node));
    impl_->connect(handle, "close-request", 4, true);
    impl_->text_bytes += title.size();
  } catch (...) {
    auto found = impl_->nodes.find(handle);
    GtkWidget *object = found == impl_->nodes.end() ? node.object : found->second.object;
    if (found != impl_->nodes.end()) { impl_->disconnect(found->second); impl_->nodes.erase(found); }
    a.gtk_window_destroy(reinterpret_cast<GtkWindow *>(object)); a.g_object_unref(object);
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
  auto &a = api();
  if (kind == "column" || kind == "row") {
    node.kind = kind == "column" ? Kind::column : Kind::row;
    node.object = a.gtk_box_new(kind == "column" ? vertical : horizontal, 0);
  } else if (kind == "grid") { node.kind = Kind::grid; node.object = a.gtk_grid_new(); }
  else if (kind == "label") { node.kind = Kind::label; node.object = a.gtk_label_new(label.c_str()); }
  else if (kind == "button") { node.kind = Kind::button; node.object = a.gtk_button_new_with_label(label.c_str()); }
  else if (kind == "entry") { node.kind = Kind::entry; node.object = a.gtk_entry_new(); }
  else if (kind == "checkbox") { node.kind = Kind::checkbox; node.object = a.gtk_check_button_new_with_label(label.c_str()); }
  else return failure("widget kind must be column, row, grid, label, button, entry, or checkbox");
  if (!node.object) return failure("GTK could not create the widget");
  a.g_object_ref_sink(node.object);
  const auto handle = fresh_handle();
  if (!handle) { a.g_object_unref(node.object); return failure("handle space exhausted"); }
  try {
    if (node.kind == Kind::entry) {
      a.gtk_entry_set_max_length(reinterpret_cast<GtkEntry *>(node.object), static_cast<int>(max_entry_characters));
      a.gtk_editable_set_text(reinterpret_cast<GtkEditable *>(node.object), label.c_str());
    }
    if (owner->kind == Kind::grid) {
      for (const auto &[id, sibling] : impl_->nodes) if (sibling.parent == parent)
        node.placement.row = std::max(node.placement.row, sibling.placement.row + sibling.placement.rows);
      if (node.placement.row > 65535) { a.g_object_unref(node.object); return failure("grid row limit exceeded"); }
    }
    impl_->nodes.emplace(handle, std::move(node));
    auto &stored = impl_->nodes.at(handle);
    if (stored.kind == Kind::button) impl_->connect(handle, "clicked", 1);
    if (stored.kind == Kind::entry) impl_->connect(handle, "changed", 2);
    if (stored.kind == Kind::checkbox) impl_->connect(handle, "toggled", 3);
    if (owner->kind == Kind::grid)
      a.gtk_grid_attach(reinterpret_cast<GtkGrid *>(owner->object), stored.object, 0, static_cast<int>(stored.placement.row), 1, 1);
    else a.gtk_box_append(reinterpret_cast<GtkBox *>(owner->kind == Kind::window ? owner->root : owner->object), stored.object);
    impl_->text_bytes += label.size();
  } catch (...) {
    auto found = impl_->nodes.find(handle);
    if (found != impl_->nodes.end()) { impl_->disconnect(found->second); a.g_object_unref(found->second.object); impl_->nodes.erase(found); }
    else a.g_object_unref(node.object);
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
  auto &a = api();
  a.gtk_grid_remove(reinterpret_cast<GtkGrid *>(parent->object), node->object);
  a.gtk_grid_attach(reinterpret_cast<GtkGrid *>(parent->object), node->object,
      static_cast<int>(column), static_cast<int>(row), static_cast<int>(column_span), static_cast<int>(row_span));
  node->placement = placement;
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
  // Allocate before touching GTK, keeping the operation unchanged on failure.
  std::string copy = text;
  auto &a = api();
  ++node->suppress;
  switch (node->kind) {
  case Kind::window: a.gtk_window_set_title(reinterpret_cast<GtkWindow *>(node->object), copy.c_str()); break;
  case Kind::label: a.gtk_label_set_text(reinterpret_cast<GtkLabel *>(node->object), copy.c_str()); break;
  case Kind::button: a.gtk_button_set_label(reinterpret_cast<GtkButton *>(node->object), copy.c_str()); break;
  case Kind::entry: a.gtk_editable_set_text(reinterpret_cast<GtkEditable *>(node->object), copy.c_str()); break;
  case Kind::checkbox: a.gtk_check_button_set_label(reinterpret_cast<GtkCheckButton *>(node->object), copy.c_str()); break;
  default: break;
  }
  --node->suppress;
  impl_->text_bytes = impl_->text_bytes - node->text.size() + copy.size();
  node->text = std::move(copy);
  return success();
}
Result Context::read_text(std::int64_t handle) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  auto *node = impl_->find(handle); if (!node) return Impl::bad_handle();
  if (container(node->kind) && node->kind != Kind::window) return failure("container does not have a text property");
  const char *text = node->kind == Kind::entry
      ? api().gtk_editable_get_text(reinterpret_cast<GtkEditable *>(node->object)) : node->text.c_str();
  if (!text) return failure("GTK returned invalid text");
  Result result; result.text = text;
  if (!valid_text(result.text)) return failure(text_error);
  return result;
}

Result Context::set_number(std::int64_t handle, const std::string &property, std::int64_t value) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  auto *node = impl_->find(handle); if (!node) return Impl::bad_handle();
  if (property == "focus") {
    if (value != 1) return failure("focus accepts only 1 to request keyboard focus");
    return api().gtk_widget_grab_focus(node->object) ? success() : failure("widget cannot receive keyboard focus");
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
  auto &a = api(); const int number = static_cast<int>(value);
  if (property == "visible") {
    if (value && node->kind == Kind::window) a.gtk_window_present(reinterpret_cast<GtkWindow *>(node->object));
    else a.gtk_widget_set_visible(node->object, number);
  } else if (property == "sensitive") a.gtk_widget_set_sensitive(node->object, number);
  else if (property == "hexpand") a.gtk_widget_set_hexpand(node->object, number);
  else if (property == "vexpand") a.gtk_widget_set_vexpand(node->object, number);
  else if (property == "checked") {
    ++node->suppress;
    a.gtk_check_button_set_active(reinterpret_cast<GtkCheckButton *>(node->object), number);
    --node->suppress;
  } else if (property == "spacing") {
    if (node->kind == Kind::grid) {
      a.gtk_grid_set_row_spacing(reinterpret_cast<GtkGrid *>(node->object), static_cast<unsigned>(number));
      a.gtk_grid_set_column_spacing(reinterpret_cast<GtkGrid *>(node->object), static_cast<unsigned>(number));
    } else a.gtk_box_set_spacing(reinterpret_cast<GtkBox *>(node->kind == Kind::window ? node->root : node->object), number);
  } else if (size) a.gtk_window_set_default_size(reinterpret_cast<GtkWindow *>(node->object),
      static_cast<int>(node->properties.at("width")), static_cast<int>(node->properties.at("height")));
  else if (margin) {
    GtkWidget *target = node->kind == Kind::window ? node->root : node->object;
    if (property == "margin" || property == "margin_start") a.gtk_widget_set_margin_start(target, number);
    if (property == "margin" || property == "margin_end") a.gtk_widget_set_margin_end(target, number);
    if (property == "margin" || property == "margin_top") a.gtk_widget_set_margin_top(target, number);
    if (property == "margin" || property == "margin_bottom") a.gtk_widget_set_margin_bottom(target, number);
  }
  return success();
}
Result Context::read_number(std::int64_t handle, const std::string &property) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  auto *node = impl_->find(handle); if (!node) return Impl::bad_handle();
  auto &a = api();
  if (property == "kind") return success(static_cast<std::int64_t>(node->kind));
  if (property == "parent") return success(node->parent);
  if (property == "checked") {
    if (node->kind != Kind::checkbox) return failure("checked requires a checkbox");
    return success(a.gtk_check_button_get_active(reinterpret_cast<GtkCheckButton *>(node->object)) ? 1 : 0);
  }
  if (property == "visible") return success(a.gtk_widget_get_visible(node->object) ? 1 : 0);
  if (property == "sensitive") return success(a.gtk_widget_get_sensitive(node->object) ? 1 : 0);
  if (property == "focus") return success(a.gtk_widget_has_focus(node->object) ? 1 : 0);
  if (property == "width" || property == "height") {
    if (node->kind == Kind::window && !a.gtk_widget_get_realized(node->object)) return success(node->properties.at(property));
    return success(property == "width" ? a.gtk_widget_get_width(node->object) : a.gtk_widget_get_height(node->object));
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
  if (!api().gtk_widget_is_sensitive(node->object)) return failure("cannot activate an insensitive widget or a child of an insensitive container");
  return api().gtk_widget_activate(node->object) ? success() : failure("GTK could not activate the widget");
}
Result Context::poll(std::int64_t window) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  auto *node = impl_->find(window); if (!node) return Impl::bad_handle();
  if (node->kind != Kind::window) return failure("poll requires a window");
  for (unsigned count = 0; count < 128; ++count) if (!api().g_main_context_iteration(nullptr, 0)) break;
  if (impl_->event_failure) {
    impl_->event_failure = false;
    return failure("event queue limit or allocation exceeded; one or more events were lost");
  }
  auto found = std::find_if(impl_->events.begin(), impl_->events.end(), [window](const auto &event) { return event.window == window; });
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
Result Context::close(std::int64_t window) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  auto *node = impl_->find(window); if (!node) return Impl::bad_handle();
  if (node->kind != Kind::window) return failure("close requires a window");
  for (auto &[id, child] : impl_->nodes) if (child.window == window) impl_->disconnect(child);
  api().gtk_window_destroy(reinterpret_cast<GtkWindow *>(node->object));
  for (auto it = impl_->nodes.begin(); it != impl_->nodes.end();) {
    if (it->second.window != window) { ++it; continue; }
    impl_->text_bytes -= it->second.text.size();
    api().g_object_unref(it->second.object); it = impl_->nodes.erase(it);
  }
  for (auto it = impl_->events.begin(); it != impl_->events.end();) {
    if (it->window != window) { ++it; continue; }
    impl_->event_bytes -= it->text.size(); it = impl_->events.erase(it);
  }
  for (auto it = impl_->snapshots.begin(); it != impl_->snapshots.end();) {
    if (it->second.window != window) { ++it; continue; }
    impl_->snapshot_bytes -= it->second.text.size(); it = impl_->snapshots.erase(it);
  }
  return success();
}
} // namespace ny::ui
