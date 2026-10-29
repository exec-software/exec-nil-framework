/*
 * Copyright (C) 2008 - 2026 Execute Software. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace ny::ui {
struct Result {
  std::int64_t number = 0;
  std::string text;
  std::string error;
  explicit operator bool() const noexcept { return error.empty(); }
};

// Checks the optional GTK 4 provider's symbols, without opening a display.
bool available() noexcept;

// Handles are nonzero, process-unique, and owned by one Context. No toolkit
// pointers cross this boundary. UI calls run on the context's creation thread
// and, once GTK initializes, the single process UI thread.
class Context {
public:
  Context();
  ~Context();
  Context(const Context &) = delete;
  Context &operator=(const Context &) = delete;
  void clear() noexcept;

  // Creates a real window with an automatic vertical root box. Explicit
  // present is set_number(window, "visible", 1); creation remains hidden.
  Result window(const std::string &title, std::int64_t width, std::int64_t height);
  // kind: column, row, grid, label, button, entry, checkbox. Parent must be a
  // window/container in this context. Grid children initially occupy successive
  // rows; grid_attach repositions an existing child of that exact grid.
  // Text is at most 1 MiB per value and 16 MiB of stored labels per context;
  // single-line entries accept at most 16384 Unicode scalar values.
  Result widget(const std::string &kind, std::int64_t parent, const std::string &label);
  Result grid_attach(std::int64_t grid, std::int64_t child, std::int64_t column,
                     std::int64_t row, std::int64_t column_span, std::int64_t row_span);
  Result set_text(std::int64_t handle, const std::string &text);
  Result read_text(std::int64_t handle);
  // Setter flags require exactly 0/1: visible, sensitive, hexpand, vexpand,
  // checked (checkbox only). focus accepts only 1 and requests keyboard focus.
  // Bounds: spacing/margin/margin_start/margin_end/
  // margin_top/margin_bottom 0..4096; width/height 1..16384 (window only).
  // Read supports those properties, plus kind, parent, width and height
  // (allocated widget size, or requested window size before realization).
  // effective_visible/effective_sensitive include ancestor flags. focus reads
  // the window's focus target, including a compound widget's internal child.
  // kind codes: window=0,column=1,row=2,grid=3,label=4,button=5,entry=6,checkbox=7.
  Result set_number(std::int64_t handle, const std::string &property, std::int64_t value);
  Result read_number(std::int64_t handle, const std::string &property);
  // Button/checkbox activation uses GTK's actual widget activation path.
  Result activate(std::int64_t handle);
  // Nonblocking: pumps a bounded number of GLib iterations, then snapshots one
  // event for this window. number==0 means no event, solely in this private ABI.
  Result poll(std::int64_t window);
  // Immutable snapshot fields: kind, target, window, checked. Event kinds:
  // 1 button activation; 2 entry changed; 3 checkbox toggled; 4 close requested.
  // Programmatic set_text/set_number(checked) suppress change notifications.
  Result event_number(std::int64_t snapshot, const std::string &field);
  Result event_text(std::int64_t snapshot);
  Result release_event(std::int64_t snapshot);
  // Removes a non-window widget and its subtree from its parent layout,
  // invalidating all descendant handles and their queued/saved events.
  // Unrelated controls and windows remain live; use close for windows.
  Result destroy_widget(std::int64_t handle);
  // Destroys a window and invalidates its descendants and queued/saved events.
  Result close(std::int64_t window);

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
} // namespace ny::ui
