/*
 * Copyright (C) 2008 - 2026 Execute Software. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
// macOS provider for the ui.v1 contract, built instead of the GTK provider on
// Darwin: one toolkit per platform. The observable contract is the one stated
// in ui_runtime.hpp and shared with the other providers through
// ui_runtime_shared.hpp -- identical handles, limits, error text, kind codes
// and event kinds. Only the toolkit beneath it differs.
//
// AppKit replaces the earlier GTK-on-macOS provider because a GTK process
// never runs -[NSApplication run]; AppKit answers accessibility requests from
// the main run loop, so such a process has no accessibility hierarchy at all
// and is invisible to assistive technology and to directed input.
//
// AppKit has no box or grid container either, so containers are plain views
// and this file owns the layout pass, exactly as the Win32 provider does.
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

#include <pthread.h>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

namespace ny::ui {
namespace {

// Control metrics. The provider owns layout, so natural sizes come from each
// control's own fitting size with the padding AppKit expects around it.
constexpr CGFloat minimum_entry_width = 120;
constexpr CGFloat control_padding = 4;

NSString *to_ns(const std::string &text) {
  NSString *value = [[NSString alloc] initWithBytes:text.data()
                                             length:static_cast<NSUInteger>(text.size())
                                           encoding:NSUTF8StringEncoding];
  return value ?: @"";
}

bool from_ns(NSString *value, std::string &out) {
  if (!value) { out.clear(); return true; }
  const char *bytes = [value UTF8String];
  if (!bytes) return false;
  out.assign(bytes);
  return true;
}

// A window server session is required before any window is created, so the
// failure names the cause instead of surfacing a late AppKit error.
bool window_server_available() {
  return CGMainDisplayID() != 0;
}

struct Platform {
  std::mutex thread_mutex;
  std::thread::id ui_thread;
  bool initialized = false;
};
Platform &platform() { static Platform *value = new Platform; return *value; }

} // namespace
} // namespace ny::ui

// Flipped so layout runs top-down, matching the other providers and the
// contract's description of rows and columns.
@interface NyUiContainer : NSView
@end
@implementation NyUiContainer
- (BOOL)isFlipped { return YES; }
@end

// The delivery block is built inside Context::Impl, which is private to
// Context; a block carries that access, where naming the type here could not.
typedef void (^NyUiDeliver)(id sender, long long kind);

@interface NyUiTarget : NSObject <NSWindowDelegate, NSTextFieldDelegate>
@property(nonatomic, copy) NyUiDeliver deliver;
- (void)buttonAction:(id)sender;
- (void)checkAction:(id)sender;
@end

@implementation NyUiTarget
- (void)buttonAction:(id)sender { if (self.deliver) self.deliver(sender, 1); }
- (void)checkAction:(id)sender { if (self.deliver) self.deliver(sender, 3); }
- (void)controlTextDidChange:(NSNotification *)notification {
  if (self.deliver) self.deliver(notification.object, 2);
}
- (BOOL)windowShouldClose:(NSWindow *)sender {
  if (self.deliver) self.deliver(sender, 4);
  // Matrix decides when to close; AppKit must not destroy a live handle.
  return NO;
}
@end

namespace ny::ui {

struct Context::Impl : std::enable_shared_from_this<Impl> {
  struct Node {
    Kind kind = Kind::window;
    NSWindow *window_object = nil; // Windows only.
    NSView *view = nil;            // Every node, including a window's root.
    std::int64_t parent = 0, window = 0;
    std::string text;
    std::map<std::string, std::int64_t> properties;
    Placement placement;
    NSRect allocation = NSZeroRect;
    unsigned suppress = 0;
    bool event_failure = false;
  };
  std::thread::id owner = std::this_thread::get_id();
  std::map<std::int64_t, Node> nodes;
  std::map<void *, std::int64_t> handles;      // view or window -> handle
  std::map<std::int64_t, std::int64_t> focus_targets;
  std::deque<Event> events;
  std::map<std::int64_t, Event> snapshots;
  std::size_t event_bytes = 0, snapshot_bytes = 0, text_bytes = 0;
  std::atomic<bool> active{true};
  bool tearing_down = false;
  NyUiTarget *target = nil;

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
    // AppKit services its windows and accessibility from the main run loop.
    if (!pthread_main_np()) return failure("AppKit must be used on the process UI thread (macOS main thread)");
    auto &p = platform();
    bool initialized;
    {
      std::lock_guard<std::mutex> lock(p.thread_mutex);
      if (p.initialized && p.ui_thread != std::this_thread::get_id())
        return failure("AppKit must be used on the process UI thread");
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
        return failure("AppKit must be used on the process UI thread");
      return success();
    }
    if (!window_server_available())
      return failure("AppKit could not open a display; a working macOS window server session is required");
    @autoreleasepool {
      NSApplication *application = [NSApplication sharedApplication];
      if (!application) return failure("AppKit could not create the shared application");
      // Accessory keeps the provider out of the Dock and the menu bar while
      // still giving real, answerable windows.
      [application setActivationPolicy:NSApplicationActivationPolicyAccessory];
      [application finishLaunching];
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

  std::int64_t handle_of(void *object) const {
    auto found = handles.find(object);
    return found == handles.end() ? 0 : found->second;
  }

  bool effective_sensitive(const Node &node) {
    for (const Node *current = &node;;) {
      const auto found = current->properties.find("sensitive");
      if (found != current->properties.end() && found->second == 0) return false;
      if (!current->parent) break;
      auto *next = find(current->parent);
      if (!next) break;
      current = next;
    }
    return true;
  }
  bool effective_visible(const Node &node) {
    for (const Node *current = &node;;) {
      const auto found = current->properties.find("visible");
      const bool shown = current->kind == Kind::window
          ? (found != current->properties.end() && found->second != 0)
          : (found == current->properties.end() || found->second != 0);
      if (!shown) return false;
      if (!current->parent) break;
      auto *next = find(current->parent);
      if (!next) break;
      current = next;
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
        auto *field = ((NSTextField *)node->view);
        if (!from_ns(field.stringValue, event.text)) { mark_failure(); return; }
        if (!valid_text(event.text)) { mark_failure(); return; }
      } else if (kind == 3) {
        auto *button = ((NSButton *)node->view);
        event.checked = button.state == NSControlStateValueOn ? 1 : 0;
      }
      if (events.size() >= max_events || event.text.size() > max_event_text_bytes - event_bytes) {
        mark_failure(); return;
      }
      events.push_back(std::move(event));
      event_bytes += events.back().text.size();
    } catch (...) { mark_failure(); }
  }

  // ---- layout -------------------------------------------------------------
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
  NSSize natural(std::int64_t handle) {
    auto *node = find(handle);
    if (!node) return NSMakeSize(0, 0);
    NSSize size = NSMakeSize(0, 0);
    switch (node->kind) {
    case Kind::label:
    case Kind::button:
    case Kind::checkbox:
    case Kind::entry: {
      size = node->view.fittingSize;
      if (node->kind == Kind::entry) size.width = std::max(size.width, minimum_entry_width);
      size.width += control_padding;
      size.height += control_padding;
      break;
    }
    default: {
      const auto kids = children_of(handle);
      const auto spacing = static_cast<CGFloat>(property(*node, "spacing"));
      if (node->kind == Kind::grid) {
        std::map<std::int64_t, CGFloat> widths, heights;
        for (auto id : kids) {
          auto *child = find(id);
          if (!child) continue;
          const auto wanted = natural(id);
          auto &width = widths[child->placement.column];
          auto &height = heights[child->placement.row];
          width = std::max(width, wanted.width / std::max<CGFloat>(1, child->placement.columns));
          height = std::max(height, wanted.height / std::max<CGFloat>(1, child->placement.rows));
        }
        for (const auto &entry : widths) size.width += entry.second;
        for (const auto &entry : heights) size.height += entry.second;
        if (!widths.empty()) size.width += spacing * (widths.size() - 1);
        if (!heights.empty()) size.height += spacing * (heights.size() - 1);
      } else {
        const bool vertical = node->kind != Kind::row;
        for (auto id : kids) {
          const auto wanted = natural(id);
          if (vertical) { size.width = std::max(size.width, wanted.width); size.height += wanted.height; }
          else { size.height = std::max(size.height, wanted.height); size.width += wanted.width; }
        }
        if (!kids.empty()) {
          const auto gaps = spacing * (kids.size() - 1);
          if (vertical) size.height += gaps; else size.width += gaps;
        }
      }
      break;
    }
    }
    if (node->kind != Kind::window) {
      size.width += property(*node, "margin_start") + property(*node, "margin_end");
      size.height += property(*node, "margin_top") + property(*node, "margin_bottom");
    }
    return size;
  }
  void place(std::int64_t handle, NSRect area) {
    auto *node = find(handle);
    if (!node) return;
    if (node->kind != Kind::window) {
      area.origin.x += property(*node, "margin_start");
      area.origin.y += property(*node, "margin_top");
      area.size.width -= property(*node, "margin_start") + property(*node, "margin_end");
      area.size.height -= property(*node, "margin_top") + property(*node, "margin_bottom");
      if (area.size.width < 0) area.size.width = 0;
      if (area.size.height < 0) area.size.height = 0;
      node->allocation = area;
      [node->view setFrame:area];
    } else {
      node->allocation = area;
    }
    if (!container(node->kind)) return;

    NSRect inner = NSMakeRect(0, 0, area.size.width, area.size.height);
    if (node->kind == Kind::window) {
      inner.origin.x += property(*node, "margin_start");
      inner.origin.y += property(*node, "margin_top");
      inner.size.width -= property(*node, "margin_start") + property(*node, "margin_end");
      inner.size.height -= property(*node, "margin_top") + property(*node, "margin_bottom");
      [node->view setFrame:NSMakeRect(0, 0, area.size.width, area.size.height)];
    }
    if (inner.size.width < 0) inner.size.width = 0;
    if (inner.size.height < 0) inner.size.height = 0;

    const auto kids = children_of(handle);
    if (kids.empty()) return;
    const auto spacing = static_cast<CGFloat>(property(*node, "spacing"));

    if (node->kind == Kind::grid) {
      std::map<std::int64_t, CGFloat> widths, heights;
      for (auto id : kids) {
        auto *child = find(id);
        if (!child) continue;
        const auto wanted = natural(id);
        auto &width = widths[child->placement.column];
        auto &height = heights[child->placement.row];
        width = std::max(width, wanted.width / std::max<CGFloat>(1, child->placement.columns));
        height = std::max(height, wanted.height / std::max<CGFloat>(1, child->placement.rows));
      }
      std::map<std::int64_t, CGFloat> column_offset, row_offset;
      CGFloat x = inner.origin.x;
      for (auto &entry : widths) { column_offset[entry.first] = x; x += entry.second + spacing; }
      CGFloat y = inner.origin.y;
      for (auto &entry : heights) { row_offset[entry.first] = y; y += entry.second + spacing; }
      for (auto id : kids) {
        auto *child = find(id);
        if (!child) continue;
        CGFloat width = 0, height = 0;
        for (std::int64_t span = 0; span < child->placement.columns; ++span) {
          auto found = widths.find(child->placement.column + span);
          if (found != widths.end()) width += found->second + (span ? spacing : 0);
        }
        for (std::int64_t span = 0; span < child->placement.rows; ++span) {
          auto found = heights.find(child->placement.row + span);
          if (found != heights.end()) height += found->second + (span ? spacing : 0);
        }
        const CGFloat left = column_offset.count(child->placement.column)
            ? column_offset[child->placement.column] : inner.origin.x;
        const CGFloat top = row_offset.count(child->placement.row)
            ? row_offset[child->placement.row] : inner.origin.y;
        place(id, NSMakeRect(left, top, width, height));
      }
      return;
    }

    const bool vertical = node->kind != Kind::row;
    CGFloat used = spacing * (kids.size() - 1);
    std::size_t expanding = 0;
    std::vector<NSSize> wanted;
    wanted.reserve(kids.size());
    for (auto id : kids) {
      wanted.push_back(natural(id));
      used += vertical ? wanted.back().height : wanted.back().width;
      auto *child = find(id);
      if (child && property(*child, vertical ? "vexpand" : "hexpand")) ++expanding;
    }
    const CGFloat span = vertical ? inner.size.height : inner.size.width;
    const CGFloat extra = span > used ? span - used : 0;
    const CGFloat share = expanding ? extra / expanding : 0;

    CGFloat offset = vertical ? inner.origin.y : inner.origin.x;
    for (std::size_t index = 0; index < kids.size(); ++index) {
      auto *child = find(kids[index]);
      if (!child) continue;
      const bool expand = property(*child, vertical ? "vexpand" : "hexpand") != 0;
      const CGFloat length = (vertical ? wanted[index].height : wanted[index].width) + (expand ? share : 0);
      NSRect target;
      if (vertical) {
        const CGFloat width = property(*child, "hexpand")
            ? inner.size.width : std::min(wanted[index].width, inner.size.width);
        target = NSMakeRect(inner.origin.x, offset, width, length);
      } else {
        const CGFloat height = property(*child, "vexpand")
            ? inner.size.height : std::min(wanted[index].height, inner.size.height);
        target = NSMakeRect(offset, inner.origin.y, length, height);
      }
      place(kids[index], target);
      offset += length + spacing;
    }
  }
  void relayout(std::int64_t window) {
    auto *node = find(window);
    if (!node || node->kind != Kind::window) return;
    NSRect content = [node->window_object contentRectForFrameRect:node->window_object.frame];
    if (content.size.width <= 0 || content.size.height <= 0)
      content = NSMakeRect(0, 0, static_cast<CGFloat>(property(*node, "width", 1)),
                           static_cast<CGFloat>(property(*node, "height", 1)));
    place(window, NSMakeRect(0, 0, content.size.width, content.size.height));
  }

  // Built here so the block closes over a weak reference with full access to
  // Impl; the target itself never names the private type.
  NyUiTarget *make_target() {
    NyUiTarget *created = [[NyUiTarget alloc] init];
    std::weak_ptr<Impl> weak = weak_from_this();
    created.deliver = ^(id sender, long long kind) {
      auto impl = weak.lock();
      if (!impl || !impl->active.load() || impl->tearing_down) return;
      const auto handle = impl->handle_of((__bridge void *)sender);
      if (handle) impl->enqueue(handle, static_cast<std::int64_t>(kind));
    };
    return created;
  }

  void dispose() noexcept {
    active.store(false);
    tearing_down = true;
    @autoreleasepool {
      for (auto &entry : nodes)
        if (entry.second.kind == Kind::window && entry.second.window_object) {
          entry.second.window_object.delegate = nil;
          [entry.second.window_object close];
          entry.second.window_object = nil;
        }
      for (auto &entry : nodes) entry.second.view = nil;
    }
    if (target) { target.deliver = nil; target = nil; }
    nodes.clear(); handles.clear(); focus_targets.clear();
    events.clear(); snapshots.clear();
    event_bytes = snapshot_bytes = text_bytes = 0;
    tearing_down = false;
  }
};


// AppKit is part of the operating system, so unlike the optional GTK provider
// there is nothing to locate or verify. Reporting availability must still not
// open a display.
bool available() noexcept { return true; }

Context::Context() : impl_(std::make_shared<Impl>()) {}
Context::~Context() { clear(); }
void Context::clear() noexcept {
  if (!impl_) return;
  auto old = std::move(impl_);
  old->active.store(false);
  if (old->owner == std::this_thread::get_id() && pthread_main_np()) old->dispose();
  else if (!old->nodes.empty()) {
    try { std::lock_guard<std::mutex> lock(Impl::deferred_mutex()); Impl::deferred().push_back(std::move(old)); }
    catch (...) { /* Callbacks hold no owning reference; a failed defer may leak windows, never free twice. */ }
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

  @autoreleasepool {
    Impl::Node node; node.kind = Kind::window; node.window = handle; node.text = title;
    node.properties = {{"width", width}, {"height", height}, {"spacing", 0}, {"visible", 0}};
    NSWindow *window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, static_cast<CGFloat>(width), static_cast<CGFloat>(height))
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    if (!window) return failure("AppKit could not create a window");
    // Creation remains hidden: ordering front happens only for visible=1.
    window.releasedWhenClosed = NO;
    window.title = to_ns(title);
    NyUiContainer *root = [[NyUiContainer alloc]
        initWithFrame:NSMakeRect(0, 0, static_cast<CGFloat>(width), static_cast<CGFloat>(height))];
    if (!root) return failure("AppKit could not create the root layout");
    window.contentView = root;
    if (!impl_->target) impl_->target = impl_->make_target();
    window.delegate = impl_->target;
    node.window_object = window;
    node.view = root;
    try {
      impl_->nodes.emplace(handle, std::move(node));
      impl_->handles.emplace((__bridge void *)window, handle);
      impl_->handles.emplace((__bridge void *)root, handle);
      impl_->text_bytes += title.size();
      impl_->relayout(handle);
    } catch (...) {
      window.delegate = nil;
      [window close];
      auto found = impl_->nodes.find(handle);
      if (found != impl_->nodes.end()) impl_->nodes.erase(found);
      return failure("window allocation failed");
    }
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

  const auto handle = fresh_handle();
  if (!handle) return failure("handle space exhausted");

  @autoreleasepool {
    Impl::Node node; node.parent = parent; node.window = owner->window; node.text = label;
    NSString *text = to_ns(label);
    if (kind == "column" || kind == "row") {
      node.kind = kind == "column" ? Kind::column : Kind::row;
      node.view = [[NyUiContainer alloc] initWithFrame:NSZeroRect];
    } else if (kind == "grid") {
      node.kind = Kind::grid;
      node.view = [[NyUiContainer alloc] initWithFrame:NSZeroRect];
    } else if (kind == "label") {
      node.kind = Kind::label;
      NSTextField *field = [[NSTextField alloc] initWithFrame:NSZeroRect];
      field.stringValue = text;
      field.editable = NO; field.selectable = NO; field.bezeled = NO; field.drawsBackground = NO;
      node.view = field;
    } else if (kind == "button") {
      node.kind = Kind::button;
      NSButton *button = [[NSButton alloc] initWithFrame:NSZeroRect];
      button.title = text;
      button.bezelStyle = NSBezelStyleRounded;
      [button setButtonType:NSButtonTypeMomentaryPushIn];
      button.target = impl_->target;
      button.action = @selector(buttonAction:);
      node.view = button;
    } else if (kind == "entry") {
      node.kind = Kind::entry;
      NSTextField *field = [[NSTextField alloc] initWithFrame:NSZeroRect];
      field.stringValue = text;
      field.editable = YES; field.selectable = YES; field.bezeled = YES;
      field.delegate = impl_->target;
      node.view = field;
    } else if (kind == "checkbox") {
      node.kind = Kind::checkbox;
      NSButton *button = [[NSButton alloc] initWithFrame:NSZeroRect];
      button.title = text;
      [button setButtonType:NSButtonTypeSwitch];
      button.target = impl_->target;
      button.action = @selector(checkAction:);
      node.view = button;
    } else return failure("widget kind must be column, row, grid, label, button, entry, or checkbox");
    if (!node.view) return failure("AppKit could not create the widget");

    if (owner->kind == Kind::grid) {
      std::vector<std::pair<std::int64_t, std::int64_t>> occupied;
      for (const auto &[id, sibling] : impl_->nodes)
        if (sibling.parent == parent && sibling.placement.column == 0)
          occupied.emplace_back(sibling.placement.row, sibling.placement.row + sibling.placement.rows);
      std::sort(occupied.begin(), occupied.end());
      for (const auto &[start, end] : occupied) {
        if (node.placement.row < start) break;
        node.placement.row = std::max(node.placement.row, end);
      }
      if (node.placement.row > 65535) return failure("grid row limit exceeded");
    }
    NSView *view = node.view;
    try {
      [owner->view addSubview:view];
      impl_->nodes.emplace(handle, std::move(node));
      impl_->handles.emplace((__bridge void *)view, handle);
      impl_->text_bytes += label.size();
      impl_->relayout(impl_->nodes.at(handle).window);
    } catch (...) {
      [view removeFromSuperview];
      auto found = impl_->nodes.find(handle);
      if (found != impl_->nodes.end()) {
        impl_->handles.erase((__bridge void *)view);
        impl_->nodes.erase(found);
      }
      return failure("widget allocation failed");
    }
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
  @autoreleasepool {
    NSString *value = to_ns(copy);
    // Programmatic writes must not raise a change event.
    ++node->suppress;
    switch (node->kind) {
    case Kind::window: node->window_object.title = value; break;
    case Kind::label:
    case Kind::entry: ((NSTextField *)node->view).stringValue = value; break;
    case Kind::button:
    case Kind::checkbox: ((NSButton *)node->view).title = value; break;
    default: break;
    }
    --node->suppress;
  }
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
  @autoreleasepool {
    if (node->kind == Kind::entry) {
      if (!from_ns(((NSTextField *)node->view).stringValue, result.text))
        return failure("AppKit returned invalid text");
    } else result.text = node->text;
  }
  if (!valid_text(result.text)) return failure(text_error);
  return result;
}

Result Context::set_number(std::int64_t handle, const std::string &property, std::int64_t value) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  auto *node = impl_->find(handle); if (!node) return Impl::bad_handle();
  if (property == "focus") {
    if (value != 1) return failure("focus accepts only 1 to request keyboard focus");
    if (node->kind != Kind::button && node->kind != Kind::entry && node->kind != Kind::checkbox)
      return failure("widget cannot receive keyboard focus");
    if (!impl_->effective_sensitive(*node)) return failure("widget cannot receive keyboard focus");
    auto *window = impl_->find(node->window);
    if (!window || !window->window_object) return failure("widget cannot receive keyboard focus");
    impl_->focus_targets[node->window] = handle;
    [window->window_object makeFirstResponder:node->view];
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

  @autoreleasepool {
    if (property == "visible") {
      if (node->kind == Kind::window) {
        if (value) [node->window_object makeKeyAndOrderFront:nil];
        else [node->window_object orderOut:nil];
        impl_->relayout(handle);
      } else {
        node->view.hidden = value ? NO : YES;
        impl_->relayout(node->window);
      }
    } else if (property == "sensitive") {
      if (!container(node->kind)) ((NSControl *)node->view).enabled = value ? YES : NO;
    } else if (property == "checked") {
      ++node->suppress;
      ((NSButton *)node->view).state = value ? NSControlStateValueOn : NSControlStateValueOff;
      --node->suppress;
    } else if (size) {
      NSRect frame = node->window_object.frame;
      NSRect content = NSMakeRect(frame.origin.x, frame.origin.y,
                                  static_cast<CGFloat>(node->properties.at("width")),
                                  static_cast<CGFloat>(node->properties.at("height")));
      [node->window_object setFrame:[node->window_object frameRectForContentRect:content] display:YES];
      impl_->relayout(handle);
    } else impl_->relayout(node->window);
  }
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
    return success(((NSButton *)node->view).state == NSControlStateValueOn ? 1 : 0);
  }
  if (property == "visible") {
    if (node->kind == Kind::window) return success(node->window_object.visible ? 1 : 0);
    return success(node->view.hidden ? 0 : 1);
  }
  if (property == "effective_visible") return success(impl_->effective_visible(*node) ? 1 : 0);
  if (property == "sensitive") {
    if (container(node->kind)) {
      auto found = node->properties.find("sensitive");
      return success(found == node->properties.end() || found->second ? 1 : 0);
    }
    return success(((NSControl *)node->view).enabled ? 1 : 0);
  }
  if (property == "effective_sensitive") return success(impl_->effective_sensitive(*node) ? 1 : 0);
  if (property == "focus") {
    auto found = impl_->focus_targets.find(node->window);
    if (found == impl_->focus_targets.end()) return success(0);
    if (found->second == handle) return success(1);
    auto *target = impl_->find(found->second);
    // A compound widget's internal child counts as the widget's own focus.
    return success(target && [target->view isDescendantOf:node->view] ? 1 : 0);
  }
  if (property == "width" || property == "height") {
    if (node->kind == Kind::window) {
      if (!node->window_object.visible) return success(node->properties.at(property));
      const NSRect content = [node->window_object contentRectForFrameRect:node->window_object.frame];
      return success(static_cast<std::int64_t>(property == "width" ? content.size.width : content.size.height));
    }
    return success(static_cast<std::int64_t>(property == "width" ? node->allocation.size.width
                                                                 : node->allocation.size.height));
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
  @autoreleasepool {
    // performClick: is the real activation path: it toggles a switch button
    // and sends the action exactly as a user click does.
    [((NSButton *)node->view) performClick:nil];
  }
  return success();
}

Result Context::poll(std::int64_t window) {
  if (!impl_) return failure("context allocation failed");
  if (auto result = impl_->check(); !result) return result;
  auto *node = impl_->find(window); if (!node) return Impl::bad_handle();
  if (node->kind != Kind::window) return failure("poll requires a window");
  @autoreleasepool {
    // Nonblocking: drain whatever AppKit already has, never wait for input.
    for (unsigned count = 0; count < 128; ++count) {
      NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                          untilDate:[NSDate distantPast]
                                             inMode:NSDefaultRunLoopMode
                                            dequeue:YES];
      if (!event) break;
      [NSApp sendEvent:event];
    }
    // Timers and other run-loop sources deliver AppKit work that is not an
    // event, so give the run loop one bounded, nonblocking turn as well.
    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate distantPast]];
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
  impl_->tearing_down = true;
  @autoreleasepool {
    [node->view removeFromSuperview];
    for (auto it = removed.rbegin(); it != removed.rend(); ++it) {
      auto found = impl_->nodes.find(*it);
      if (found == impl_->nodes.end()) continue;
      impl_->handles.erase((__bridge void *)found->second.view);
      impl_->text_bytes -= found->second.text.size();
      found->second.view = nil;
      impl_->nodes.erase(found);
    }
  }
  impl_->tearing_down = false;

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

  impl_->tearing_down = true;
  @autoreleasepool {
    node->window_object.delegate = nil;
    [node->window_object close];
    impl_->handles.erase((__bridge void *)node->window_object);
    for (auto it = impl_->nodes.begin(); it != impl_->nodes.end();) {
      if (it->second.window != window) { ++it; continue; }
      impl_->handles.erase((__bridge void *)it->second.view);
      impl_->text_bytes -= it->second.text.size();
      it->second.view = nil;
      it->second.window_object = nil;
      it = impl_->nodes.erase(it);
    }
  }
  impl_->tearing_down = false;

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
