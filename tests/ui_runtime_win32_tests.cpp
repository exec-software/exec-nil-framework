/*
 * Copyright (C) 2008 - 2026 Execute Software. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
// Windows provider contract for ui.v1. The GTK fixture in ui_runtime_tests.cpp
// inspects loaded GTK modules and symbol origins, so it describes that
// provider rather than this one; Windows gets its own fixture for the same
// published contract.
//
// Modes match what tests/tools/windows_ui_display_contract.py invokes:
//   --provider-present  availability only, no window
//   --display           the full contract against a real desktop
// With no argument the fixture adapts: it runs the full contract when the
// process has an interactive window station and otherwise checks that window
// creation is refused, so the same test is meaningful in a service session.
#include "ui_runtime.hpp"

#include <cstring>
#include <iostream>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

int checks = 0;

void require(bool condition, const char *what) {
  ++checks;
  if (condition) return;
  std::cout.flush();
  std::cerr << "UI runtime test failure: " << what << "\n";
  std::exit(1);
}

std::int64_t ok(const ny::ui::Result &result, const char *what) {
  if (!result) {
    std::cout.flush();
    std::cerr << "UI runtime test failure: " << what << ": " << result.error << "\n";
    std::exit(1);
  }
  ++checks;
  return result.number;
}

void rejected(const ny::ui::Result &result, const char *fragment, const char *what) {
  ++checks;
  if (!result.error.empty() && result.error.find(fragment) != std::string::npos) return;
  std::cout.flush();
  std::cerr << "UI runtime test failure: " << what << ": expected rejection containing '"
            << fragment << "', got '" << result.error << "'\n";
  std::exit(1);
}

bool interactive_station() {
  const HWINSTA station = GetProcessWindowStation();
  if (!station) return false;
  USEROBJECTFLAGS flags{};
  DWORD needed = 0;
  if (!GetUserObjectInformationW(station, UOI_FLAGS, &flags, sizeof flags, &needed)) return false;
  return (flags.dwFlags & WSF_VISIBLE) != 0;
}

// Drains one event for the window, pumping until it appears or the bound is
// reached. Activation delivers WM_COMMAND synchronously, so this is a guard
// against a lost event rather than a wait loop.
std::int64_t next_event(ny::ui::Context &context, std::int64_t window) {
  for (int attempt = 0; attempt < 32; ++attempt) {
    const auto polled = context.poll(window);
    if (!polled) return 0;
    if (polled.number) return polled.number;
    Sleep(5);
  }
  return 0;
}

void contract() {
  ny::ui::Context context;
  const auto window = ok(context.window("NIL Win32 provider", 420, 320), "create window");
  require(window > 0, "window handle is nonzero");
  require(ok(context.read_number(window, "kind"), "window kind") == 0, "window reports kind 0");
  require(ok(context.read_number(window, "parent"), "window parent") == 0, "window has no parent");

  // Creation is hidden until visible is set, and width/height report the
  // requested size while the window is unrealized.
  require(ok(context.read_number(window, "visible"), "window visible") == 0, "window starts hidden");
  require(ok(context.read_number(window, "width"), "window width") == 420, "window reports requested width");
  require(ok(context.read_number(window, "height"), "window height") == 320, "window reports requested height");

  const auto column = ok(context.widget("column", window, ""), "create column");
  require(ok(context.read_number(column, "kind"), "column kind") == 1, "column reports kind 1");
  require(ok(context.read_number(column, "parent"), "column parent") == window, "column parent is the window");

  const auto label = ok(context.widget("label", column, "Hello"), "create label");
  const auto button = ok(context.widget("button", column, "Press"), "create button");
  const auto check = ok(context.widget("checkbox", column, "Check"), "create checkbox");
  const auto entry = ok(context.widget("entry", column, "abc"), "create entry");

  const auto text = context.read_text(label);
  require(ok(text, "read label") == 0 && text.text == "Hello", "label keeps its text");
  const auto typed = context.read_text(entry);
  require(ok(typed, "read entry") == 0 && typed.text == "abc", "entry reports its live text");

  // Non-ASCII must survive the UTF-8/UTF-16 conversion in both directions.
  const std::string unicode = "caf\xc3\xa9 \xe4\xb8\xad\xe6\x96\x87";
  ok(context.set_text(label, unicode), "set unicode label");
  const auto round_trip = context.read_text(label);
  require(ok(round_trip, "read unicode label") == 0 && round_trip.text == unicode,
          "label round-trips non-ASCII text");

  ok(context.set_number(window, "visible", 1), "present window");
  require(ok(context.read_number(window, "visible"), "window visible") == 1, "window reports visible");
  require(ok(context.read_number(label, "effective_visible"), "label effective visible") == 1,
          "a child of a shown window is effectively visible");

  // The provider owns layout on Win32, so a realized child must have a real
  // allocation rather than the zero size USER32 creates it with.
  require(ok(context.read_number(entry, "width"), "entry width") > 0, "provider allocates entry width");
  require(ok(context.read_number(entry, "height"), "entry height") > 0, "provider allocates entry height");

  // Button activation travels the real BM_CLICK path and reports one event.
  require(next_event(context, window) == 0, "no event is queued before activation");
  ok(context.activate(button), "activate button");
  const auto snapshot = next_event(context, window);
  require(snapshot != 0, "button activation produces an event");
  require(ok(context.event_number(snapshot, "kind"), "event kind") == 1, "button activation is event kind 1");
  require(ok(context.event_number(snapshot, "target"), "event target") == button, "event targets the button");
  require(ok(context.event_number(snapshot, "window"), "event window") == window, "event names its window");
  ok(context.release_event(snapshot), "release button event");

  // An auto checkbox toggles through the same path and reports its state.
  require(ok(context.read_number(check, "checked"), "checkbox state") == 0, "checkbox starts clear");
  ok(context.activate(check), "activate checkbox");
  const auto toggled = next_event(context, window);
  require(toggled != 0, "checkbox activation produces an event");
  require(ok(context.event_number(toggled, "kind"), "toggle kind") == 3, "checkbox toggle is event kind 3");
  require(ok(context.event_number(toggled, "checked"), "toggle checked") == 1, "toggle reports the new state");
  require(ok(context.read_number(check, "checked"), "checkbox state") == 1, "checkbox is now set");
  ok(context.release_event(toggled), "release toggle event");

  // Programmatic writes must stay silent, or a model binding would see its
  // own update echoed back as user input.
  ok(context.set_text(entry, "quiet"), "set entry text");
  require(next_event(context, window) == 0, "programmatic set_text raises no event");
  ok(context.set_number(check, "checked", 0), "clear checkbox");
  require(next_event(context, window) == 0, "programmatic checked raises no event");

  ok(context.set_number(entry, "focus", 1), "focus entry");
  require(ok(context.read_number(entry, "focus"), "entry focus") == 1, "entry reports keyboard focus");

  // Sensitivity is inherited, and an insensitive control cannot be activated.
  ok(context.set_number(column, "sensitive", 0), "disable column");
  require(ok(context.read_number(button, "effective_sensitive"), "button effective sensitive") == 0,
          "a child of an insensitive container is insensitive");
  rejected(context.activate(button), "insensitive", "activate insensitive button");
  ok(context.set_number(column, "sensitive", 1), "re-enable column");

  // Grid placement, automatic rows and overlap rejection.
  const auto grid = ok(context.widget("grid", window, ""), "create grid");
  const auto first = ok(context.widget("label", grid, "one"), "create grid child one");
  const auto second = ok(context.widget("label", grid, "two"), "create grid child two");
  ok(context.grid_attach(grid, first, 0, 0, 1, 1), "attach first grid child");
  rejected(context.grid_attach(grid, second, 0, 0, 1, 1), "overlaps", "overlapping grid placement");
  ok(context.grid_attach(grid, second, 1, 0, 1, 1), "attach second grid child");

  // Rejected inputs keep their published wording.
  rejected(context.widget("menu", window, ""), "widget kind", "unknown widget kind");
  rejected(context.widget("label", 0, ""), "handle", "unknown parent handle");
  rejected(context.set_number(button, "visible", 2), "0 or 1", "non-boolean flag");
  rejected(context.set_number(column, "spacing", 9000), "4096", "out-of-range spacing");
  rejected(context.set_number(button, "checked", 1), "checkbox", "checked on a button");
  rejected(context.read_number(column, "text"), "numeric property", "unknown numeric property");
  rejected(context.read_text(column), "container", "text on a container");
  rejected(context.destroy_widget(window), "non-window", "destroy_widget on a window");
  rejected(context.poll(button), "window", "poll on a widget");
  rejected(context.set_text(label, std::string("\xff\xfe")), "UTF-8", "invalid UTF-8 text");

  // Destroying a subtree invalidates its descendants but nothing else.
  const auto doomed = ok(context.widget("row", column, ""), "create row");
  const auto child = ok(context.widget("label", doomed, "gone"), "create row child");
  ok(context.destroy_widget(doomed), "destroy row");
  rejected(context.read_number(child, "kind"), "handle", "descendant handle is invalidated");
  require(ok(context.read_number(label, "kind"), "surviving label") == 4, "unrelated widgets survive");

  // Closing the window invalidates everything inside it.
  ok(context.close(window), "close window");
  rejected(context.read_number(label, "kind"), "handle", "closed window invalidates its children");
  rejected(context.close(window), "handle", "closing twice is an error");
}

} // namespace

int main(int argc, char **argv) {
  const std::string mode = argc > 1 ? argv[1] : "";
  require(ny::ui::available(), "Windows provider is always available");
  if (mode == "--provider-present") {
    std::cout << "UI runtime: " << checks << " checks passed (--provider-present)\n";
    return 0;
  }
  const bool desktop = interactive_station();
  if (mode == "--display") {
    require(desktop, "--display requires an interactive window station");
    contract();
    std::cout << "UI runtime: " << checks << " checks passed (--display)\n";
    return 0;
  }
  if (desktop) contract();
  else {
    // A service session has no desktop: creation must fail, and say why.
    ny::ui::Context context;
    rejected(context.window("no station", 320, 240), "interactive Windows window station",
             "window creation without an interactive station");
  }
  std::cout << "UI runtime: " << checks << " checks passed ("
            << (desktop ? "desktop" : "no-desktop") << ")\n";
  return 0;
}
