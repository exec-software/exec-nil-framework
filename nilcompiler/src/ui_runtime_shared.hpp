/*
 * Copyright (C) 2008 - 2026 Execute Software. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#pragma once
// Toolkit-independent parts of the ui.v1 contract: handle allocation, text
// validation, kind codes, event/placement records and the stored limits.
// Exactly one provider translation unit is compiled per platform (GTK on
// Linux, Win32 on Windows), so the internal linkage below is one definition
// per process and handles stay process-unique. Keeping these here is what
// makes a second provider reproduce the first provider's semantics -- limits,
// error text and validation -- instead of restating them and drifting.
#include "ui_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <string>

namespace ny::ui {
namespace {

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
} // namespace ny::ui
