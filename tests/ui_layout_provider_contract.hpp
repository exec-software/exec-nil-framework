#pragma once
#include "ui_runtime.hpp"
#include <chrono>
#include <string>
#include <thread>

// The same public-provider assertions run on GTK, Win32 and AppKit. Compare
// allocation changes and equal-control requisitions, never font-specific pixels.
template <class Check>
void ui_layout_provider_contract(Check check) {
  ny::ui::Context context;
  const auto ok = [&](const ny::ui::Result &result) {
    check(static_cast<bool>(result), result.error.c_str()); return result.number;
  };
  const auto width = [&](std::int64_t widget) { return ok(context.read_number(widget, "width")); };
  const auto height = [&](std::int64_t widget) { return ok(context.read_number(widget, "height")); };
  const auto await = [&](std::int64_t window, auto condition, const char *message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    bool passed = false;
    do {
      const auto event = ok(context.poll(window));
      if (event) ok(context.release_event(event));
      if (condition()) { passed = true; break; }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < deadline);
    check(passed, message);
  };

  const auto window = ok(context.window("NIL layout visibility and expansion", 720, 240));
  const auto row = ok(context.widget("row", window, ""));
  const auto fixed = ok(context.widget("button", row, "Fixed sibling"));
  const auto nested = ok(context.widget("column", row, ""));
  const auto entry = ok(context.widget("entry", nested, "entry"));
  ok(context.set_number(row, "spacing", 7));
  ok(context.set_number(entry, "hexpand", 1));
  ok(context.set_number(window, "visible", 1));
  await(window, [&] { return width(row) > 600 && width(entry) > 300 && width(fixed) > 0; },
        "visible descendants propagate expansion through nested containers");
  const auto before_hidden = width(entry), fixed_width = width(fixed);
  ok(context.set_number(fixed, "visible", 0));
  await(window, [&] { return width(entry) >= before_hidden + fixed_width; },
        "hiding a child releases its size and spacing to the visible expanding sibling");
  ok(context.set_number(fixed, "visible", 1));
  await(window, [&] { return width(entry) == before_hidden; }, "showing a child restores the original layout");
  const auto expanded = width(nested);
  ok(context.set_number(nested, "hexpand", 0));
  await(window, [&] { return width(nested) < expanded; }, "explicit false overrides descendant expansion");
  ok(context.set_number(nested, "hexpand", 1));
  await(window, [&] { return width(nested) == expanded; }, "explicit true restores expansion");
  ok(context.set_number(window, "width", 900));
  await(window, [&] { return width(entry) > before_hidden + 100; }, "resizing reaches nested expanding controls");
  ok(context.close(window));

  const auto grid_window = ok(context.window("NIL layout spans and grid growth", 640, 320));
  const auto reference_row = ok(context.widget("row", grid_window, ""));
  const std::string title = "A complete spanning control";
  const auto reference = ok(context.widget("button", reference_row, title));
  const auto grid = ok(context.widget("grid", grid_window, ""));
  const auto spanning = ok(context.widget("button", grid, title));
  ok(context.grid_attach(grid, spanning, 0, 0, 3, 2));
  ok(context.set_number(grid, "spacing", 3));
  ok(context.set_number(grid_window, "visible", 1));
  await(grid_window, [&] { return width(reference) > 0 && height(reference) > 0 &&
      width(spanning) >= width(reference) && height(spanning) >= height(reference); },
        "a lone multi-column and multi-row child preserves its full natural requisition");
  const auto fixed_span_width = width(spanning), fixed_span_height = height(spanning);
  ok(context.set_number(spanning, "hexpand", 1));
  ok(context.set_number(spanning, "vexpand", 1));
  await(grid_window, [&] { return width(spanning) > fixed_span_width + 100 && height(spanning) > fixed_span_height + 100; },
        "grid tracks distribute available horizontal and vertical space to expanding spans");
  const auto old_grid_width = width(spanning), old_grid_height = height(spanning);
  ok(context.set_number(grid_window, "width", 800));
  ok(context.set_number(grid_window, "height", 480));
  await(grid_window, [&] { return width(spanning) > old_grid_width + 100 && height(spanning) > old_grid_height + 100; },
        "native grid allocations continue to expand after resizing");
  ok(context.close(grid_window));

  const auto fill_window = ok(context.window("NIL layout cross-axis fill", 480, 180));
  const auto label = ok(context.widget("label", fill_window, "fill"));
  ok(context.set_number(label, "hexpand", 0));
  ok(context.set_number(fill_window, "visible", 1));
  await(fill_window, [&] { return width(label) == width(fill_window) && width(label) > 400; },
        "default cross-axis fill is independent of requesting extra major-axis space");
  ok(context.close(fill_window));
}
