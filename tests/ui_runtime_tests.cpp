/*
 * Copyright (C) 2008 - 2026 Execute Software. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#include "ui_runtime.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#if defined(__linux__)
#include <dlfcn.h>
#endif

namespace {
int checks = 0;
void require(bool condition, const std::string &message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
std::int64_t ok(const ny::ui::Result &result) {
  require(static_cast<bool>(result), result.error);
  return result.number;
}
void rejected(const ny::ui::Result &result, const std::string &message) {
  require(!result && result.error.find(message) != std::string::npos,
      "expected rejection containing '" + message + "', got '" + result.error + "'");
}
std::int64_t wait_event(ny::ui::Context &context, std::int64_t window) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < deadline) {
    const auto event = ok(context.poll(window));
    if (event) return event;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  throw std::runtime_error("timed out waiting for GTK activation event");
}
void basic_validation() {
  ny::ui::Context context;
  rejected(context.window("invalid", 0, 100), "between 1 and 16384");
  rejected(context.window("invalid", 100, 16385), "between 1 and 16384");
  rejected(context.window(std::string("bad\0title", 9), 100, 100), "without NUL");
  rejected(context.window(std::string("\xc0\x80", 2), 100, 100), "UTF-8");
  rejected(context.window(std::string("\xed\xa0\x80", 3), 100, 100), "UTF-8");
  rejected(context.window(std::string("\xf4\x90\x80\x80", 4), 100, 100), "UTF-8");
  rejected(context.window(std::string(1048577, 'x'), 100, 100), "1048576");
  rejected(context.read_text(1), "foreign");
  rejected(context.destroy_widget(1), "foreign");
  rejected(context.set_number(-1, "checked", 1), "foreign");
  rejected(context.event_text(1), "snapshot");
  rejected(context.release_event(1), "snapshot");
  ny::ui::Result wrong_thread;
  std::thread worker([&] { wrong_thread = context.poll(1); }); worker.join();
  rejected(wrong_thread, "creation thread");
}

void no_display() {
#if defined(__linux__)
  // This mode is deliberately independent of the developer's desktop.
  unsetenv("WAYLAND_DISPLAY");
  setenv("DISPLAY", ":65534", 1);
  setenv("GDK_BACKEND", "x11", 1);
#endif
  ny::ui::Context context;
  auto result = context.window("unavailable display", 300, 200);
  rejected(result, ny::ui::available() ? "display" : "provider");
}

void subtree_lifetime_tests(ny::ui::Context &context) {
  const auto window = ok(context.window("Dynamic widget lifecycle", 320, 240));
  const auto elsewhere = ok(context.window("Unrelated window", 160, 100));
  const auto unrelated = ok(context.widget("checkbox", elsewhere, "Keep this event"));
  ok(context.activate(unrelated));
  const auto unrelated_snapshot = wait_event(context, elsewhere);

  const auto sibling = ok(context.widget("checkbox", window, "Keep this sibling"));
  const auto subtree = ok(context.widget("column", window, ""));
  const auto nested = ok(context.widget("row", subtree, ""));
  const auto checkbox = ok(context.widget("checkbox", nested, "Remove this control"));
  const auto label = ok(context.widget("label", nested, "Remove this label"));
  ok(context.activate(sibling));
  const auto sibling_snapshot = wait_event(context, window);
  ok(context.activate(checkbox));
  const auto removed_snapshot = wait_event(context, window);
  // Leave a second event queued on each side of the removal boundary.
  ok(context.activate(checkbox));
  ok(context.activate(sibling));
  rejected(context.destroy_widget(window), "use close for windows");
  require(ok(context.event_number(removed_snapshot, "target")) == checkbox,
      "rejected window destruction must preserve descendant snapshots");
  ny::ui::Context other_context;
  rejected(other_context.destroy_widget(subtree), "foreign");
  ok(context.destroy_widget(subtree));
  for (auto handle : {subtree, nested, checkbox, label}) {
    rejected(context.read_number(handle, "kind"), "closed");
    rejected(context.destroy_widget(handle), "closed");
    rejected(context.widget("label", handle, "Stale parent"), "closed");
  }
  rejected(context.event_number(removed_snapshot, "target"), "snapshot");
  rejected(context.release_event(removed_snapshot), "snapshot");
  require(ok(context.event_number(sibling_snapshot, "target")) == sibling,
      "sibling event snapshots survive subtree removal");
  require(ok(context.event_number(unrelated_snapshot, "target")) == unrelated,
      "another window's event snapshots survive subtree removal");
  const auto queued_sibling = wait_event(context, window);
  require(ok(context.event_number(queued_sibling, "target")) == sibling,
      "only events targeting removed descendants are discarded");
  require(ok(context.poll(window)) == 0, "removed subtree must not retain queued events");
  ok(context.release_event(queued_sibling));
  ok(context.release_event(sibling_snapshot));
  ok(context.release_event(unrelated_snapshot));
  const auto replacement = ok(context.widget("column", window, ""));
  require(replacement != subtree, "removed subtree handle must never be reused");
  ok(context.widget("entry", replacement, "Replacement control"));
  ok(context.destroy_widget(replacement));
  // The surviving window/root can still create and dispatch controls.
  ok(context.activate(sibling));
  const auto final_sibling = wait_event(context, window);
  require(ok(context.event_number(final_sibling, "target")) == sibling, "surviving sibling remains interactive");
  ok(context.release_event(final_sibling));
  ok(context.close(window));
  require(context.read_text(unrelated).text == "Keep this event", "closing one window preserves another");
  ok(context.close(elsewhere));

  const auto grid_window = ok(context.window("Dynamic grid lifecycle", 200, 140));
  const auto grid = ok(context.widget("grid", grid_window, ""));
  const auto last = ok(context.widget("label", grid, "Last row"));
  ok(context.grid_attach(grid, last, 0, 65535, 1, 1));
  const auto first = ok(context.widget("column", grid, ""));
  ok(context.widget("label", first, "Nested grid child"));
  // The explicit last row must leave room for automatic placement at row zero.
  rejected(context.grid_attach(grid, last, 0, 0, 1, 1), "overlaps");
  ok(context.destroy_widget(first));
  const auto replacement_first = ok(context.widget("label", grid, "First row reused"));
  rejected(context.grid_attach(grid, last, 0, 0, 1, 1), "overlaps");
  ok(context.destroy_widget(replacement_first));
  ok(context.grid_attach(grid, last, 0, 0, 1, 65536));
  rejected(context.widget("label", grid, "No free rows"), "grid row limit");
  ok(context.destroy_widget(last));
  ok(context.widget("label", grid, "Recovered all grid rows"));
  ok(context.destroy_widget(grid));
  ok(context.close(grid_window));
}

void event_overflow_tests(ny::ui::Context &context) {
  const auto crowded = ok(context.window("Crowded event queue", 180, 100));
  const auto quiet = ok(context.window("Quiet event queue", 180, 100));
  const auto toggle = ok(context.widget("checkbox", crowded, "Fill event queue"));
  for (int index = 0; index < 4097; ++index) ok(context.activate(toggle));
  require(ok(context.poll(quiet)) == 0, "a quiet window must not consume another window's lost-event error");
  rejected(context.poll(crowded), "events were lost");
  ok(context.destroy_widget(toggle));
  require(ok(context.poll(crowded)) == 0, "removing a control releases its queued events");
  const auto recovered = ok(context.widget("checkbox", quiet, "Recovered event queue"));
  ok(context.activate(recovered));
  const auto event = wait_event(context, quiet);
  require(ok(context.event_number(event, "target")) == recovered, "event queue recovers after subtree removal");
  ok(context.release_event(event));
  ok(context.close(crowded));
  ok(context.close(quiet));
}

void display_tests() {
  require(ny::ui::available(), "GTK 4 provider is required for --display tests");
  ny::ui::Context context;
  auto window = ok(context.window("NIL UI provider contract", 420, 320));
  require(window > 0, "window must have a nonzero handle");
  require(ok(context.read_number(window, "visible")) == 0, "window must start hidden");
  require(ok(context.read_number(window, "width")) == 420, "hidden window width");
  require(ok(context.read_number(window, "height")) == 320, "hidden window height");
  auto column = ok(context.widget("column", window, ""));
  auto row = ok(context.widget("row", column, ""));
  auto label = ok(context.widget("label", row, "NIL — Matrix ✓"));
  auto entry = ok(context.widget("entry", column, "naïve 🌍"));
  auto checkbox = ok(context.widget("checkbox", column, "Enabled"));
  auto button = ok(context.widget("button", column, "Apply"));
  auto grid = ok(context.widget("grid", column, ""));
  auto first = ok(context.widget("label", grid, "First"));
  auto second = ok(context.widget("label", grid, "Second"));
  require(ok(context.read_number(entry, "kind")) == 6, "entry kind code");
  require(ok(context.read_number(entry, "parent")) == column, "parent identity");
  require(context.read_text(entry).text == "naïve 🌍", "entry text UTF-8 roundtrip");
  require(context.read_text(label).text == "NIL — Matrix ✓", "label text UTF-8 roundtrip");
  rejected(context.widget("unsupported", column, ""), "widget kind");
  rejected(context.widget("button", label, "bad parent"), "parent must");
  rejected(context.set_text(row, "not text"), "container");
  rejected(context.widget("entry", column, std::string(16385, 'a')), "16384");
  rejected(context.set_text(entry, std::string(16385, 'a')), "16384");
  rejected(context.set_text(entry, std::string("bad\0entry", 9)), "without NUL");
  rejected(context.set_text(entry, std::string("\xf0\x80", 2)), "UTF-8");
  require(context.read_text(entry).text == "naïve 🌍", "rejected setter must preserve text");
  rejected(context.set_number(checkbox, "checked", 2), "exactly 0 or 1");
  require(ok(context.read_number(checkbox, "checked")) == 0, "invalid Boolean must not mutate");
  rejected(context.set_number(label, "checked", 1), "checkbox");
  rejected(context.read_number(label, "checked"), "checkbox");
  rejected(context.set_number(label, "width", 100), "window properties");
  rejected(context.set_number(button, "spacing", 5), "spacing requires");
  rejected(context.set_number(column, "margin", -1), "between 0 and 4096");
  rejected(context.set_number(column, "unknown", 1), "unknown");
  rejected(context.read_number(column, "unknown"), "unknown");
  rejected(context.set_number(entry, "focus", 0), "only 1");
  ok(context.set_number(column, "spacing", 8));
  require(ok(context.read_number(column, "spacing")) == 8, "container spacing");
  ok(context.set_number(column, "margin_start", 3));
  rejected(context.read_number(column, "margin"), "margins differ");
  ok(context.set_number(column, "margin", 12));
  require(ok(context.read_number(column, "margin_start")) == 12, "uniform margin overrides earlier side");
  require(ok(context.read_number(column, "margin")) == 12, "uniform margin read");
  ok(context.set_number(entry, "hexpand", 1));
  require(ok(context.read_number(entry, "hexpand")) == 1, "expansion property");
  ok(context.grid_attach(grid, second, 1, 0, 1, 1));
  rejected(context.grid_attach(grid, first, 1, 0, 1, 1), "overlaps");
  rejected(context.grid_attach(grid, first, 65535, 0, 2, 1), "coordinates");
  rejected(context.grid_attach(grid, button, 3, 0, 1, 1), "child of");

  // Setters update actual toolkit state without feeding back user change events.
  ok(context.set_text(entry, "Matrix"));
  ok(context.set_number(checkbox, "checked", 1));
  require(context.read_text(entry).text == "Matrix", "programmatic entry update");
  require(ok(context.read_number(checkbox, "checked")) == 1, "programmatic checkbox update");
  require(ok(context.poll(window)) == 0, "programmatic setters must suppress feedback");
  ok(context.set_number(window, "visible", 1));
  ok(context.set_number(entry, "focus", 1));
  for (int index = 0; index < 20; ++index) {
    require(ok(context.poll(window)) == 0, "window rendering should not synthesize control events");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  require(ok(context.read_number(entry, "width")) > 0, "GTK must allocate entry width");
  require(ok(context.read_number(entry, "focus")) == 1, "entry focus includes its internal text widget");
  require(ok(context.read_number(label, "focus")) == 0, "unfocused label does not own focus");
  ok(context.set_number(checkbox, "focus", 1));
  require(ok(context.read_number(checkbox, "focus")) == 1, "focus moves to a checkbox");
  require(ok(context.read_number(entry, "focus")) == 0, "entry loses logical focus after transfer");
  require(ok(context.read_number(button, "visible")) == 1, "button local visibility");
  require(ok(context.read_number(button, "effective_visible")) == 1, "shown ancestors make button effectively visible");
  ok(context.set_number(column, "visible", 0));
  require(ok(context.read_number(button, "visible")) == 1, "hidden ancestor preserves local visibility flag");
  require(ok(context.read_number(button, "effective_visible")) == 0, "hidden ancestor suppresses effective visibility");
  rejected(context.set_number(button, "effective_visible", 1), "read-only");
  ok(context.set_number(column, "visible", 1));

  ok(context.activate(checkbox));
  auto snapshot = wait_event(context, window);
  require(ok(context.event_number(snapshot, "kind")) == 3, "real checkbox activation event");
  require(ok(context.event_number(snapshot, "checked")) == 0, "checkbox toggles from active to false");
  require(ok(context.event_number(snapshot, "target")) == checkbox, "event identifies checkbox");
  require(ok(context.event_number(snapshot, "window")) == window, "event identifies window");
  ok(context.set_number(checkbox, "checked", 1));
  require(ok(context.event_number(snapshot, "checked")) == 0, "snapshot survives later state changes");
  rejected(context.event_number(snapshot, "unknown"), "event field");
  ok(context.release_event(snapshot));
  rejected(context.release_event(snapshot), "released");
  rejected(context.event_text(snapshot), "released");

  ok(context.set_number(button, "sensitive", 0));
  rejected(context.activate(button), "insensitive");
  ok(context.set_number(button, "sensitive", 1));
  ok(context.set_number(column, "sensitive", 0));
  require(ok(context.read_number(button, "sensitive")) == 1, "insensitive ancestor preserves local sensitivity flag");
  require(ok(context.read_number(button, "effective_sensitive")) == 0, "insensitive ancestor suppresses effective sensitivity");
  rejected(context.set_number(button, "effective_sensitive", 1), "read-only");
  rejected(context.activate(button), "insensitive container");
  rejected(context.activate(checkbox), "insensitive container");
  require(ok(context.poll(window)) == 0, "disabled ancestor must prevent activation events");
  ok(context.set_number(column, "sensitive", 1));
  ok(context.activate(button));
  snapshot = wait_event(context, window);
  require(ok(context.event_number(snapshot, "kind")) == 1, "real button activation event");
  require(ok(context.event_number(snapshot, "target")) == button, "button event target");
  require(context.event_text(snapshot).text.empty(), "button event has no invented text");

  ny::ui::Context other;
  rejected(other.read_text(entry), "foreign");
  rejected(other.event_text(snapshot), "foreign");
  ny::ui::Result thread_error;
  std::thread foreign_thread([&] {
    ny::ui::Context foreign_context;
    thread_error = foreign_context.window("Wrong thread", 100, 100);
  }); foreign_thread.join();
  rejected(thread_error, "process UI thread");

  subtree_lifetime_tests(context);
  event_overflow_tests(context);

  ok(context.close(window));
  rejected(context.close(window), "closed");
  rejected(context.read_text(entry), "closed");
  rejected(context.event_text(snapshot), "snapshot");
  const auto replacement = ok(context.window("Fresh handles", 200, 100));
  require(replacement != window, "closed handles must never be reused");
  context.clear();
  rejected(context.poll(replacement), "closed");

#if defined(__linux__)
  // Observe GTK's actual top-level registry across normal and deferred teardown.
  void *library = dlopen("libgtk-4.so.1", RTLD_NOW | RTLD_LOCAL);
  auto windows = reinterpret_cast<void *(*)()>(dlsym(library, "gtk_window_get_toplevels"));
  auto count = reinterpret_cast<unsigned (*)(void *)>(dlsym(library, "g_list_model_get_n_items"));
  require(windows && count, "GTK top-level list symbols");
  const auto baseline = count(windows());
  for (int index = 0; index < 8; ++index) {
    auto owner = std::make_unique<ny::ui::Context>();
    const auto owned_window = ok(owner->window("Lifetime check", 150, 100));
    ok(owner->widget("button", owned_window, "Callback lifetime"));
    require(count(windows()) == baseline + 1, "real GTK window registration");
    if (index % 2) {
      std::thread destroy_elsewhere([moved = std::move(owner)]() mutable { moved.reset(); });
      destroy_elsewhere.join();
      // Any subsequent call on the process UI thread drains deferred teardown.
      rejected(context.read_text(-1), "foreign");
    } else owner.reset();
    require(count(windows()) == baseline, "context teardown must release GTK top-level windows");
  }
  dlclose(library);
#endif
}
} // namespace

int main(int argc, char **argv) {
  try {
    require(argc <= 2, "usage: ui_runtime_tests [--no-display|--display]");
    const std::string mode = argc == 2 ? argv[1] : "--no-display";
    require(mode == "--no-display" || mode == "--display", "unknown UI test mode");
    basic_validation();
    if (mode == "--display") display_tests(); else no_display();
    std::cout << "UI runtime: " << checks << " checks passed (" << mode << ")\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "UI runtime test failure: " << error.what() << '\n';
    return 1;
  }
}
