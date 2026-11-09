/*
 * Copyright (C) 2008 - 2026 Execute Software. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
// macOS provider contract for ui.v1, the Darwin counterpart of
// tests/ui_runtime_win32_tests.cpp. The GTK fixture inspects loaded GTK
// modules and symbol origins, so it describes that provider and cannot speak
// for AppKit.
//
// Modes: --provider-present reports availability without opening a display,
// --display runs the full contract against a window server session. With no
// argument the fixture adapts, so a session-less runner still checks that
// window creation is refused rather than silently skipping everything.
//
// main() runs on the process main thread, which is where AppKit requires the
// provider to be used.
#include "ui_runtime.hpp"

#include <iostream>
#include <string>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

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

bool window_server() { return CGMainDisplayID() != 0; }

std::int64_t next_event(ny::ui::Context &context, std::int64_t window) {
  for (int attempt = 0; attempt < 64; ++attempt) {
    const auto polled = context.poll(window);
    if (!polled) return 0;
    if (polled.number) return polled.number;
  }
  return 0;
}

void contract() {
  ny::ui::Context context;
  const auto window = ok(context.window("NIL AppKit provider", 420, 320), "create window");
  require(window > 0, "window handle is nonzero");
  require(ok(context.read_number(window, "kind"), "window kind") == 0, "window reports kind 0");
  require(ok(context.read_number(window, "parent"), "window parent") == 0, "window has no parent");
  require(ok(context.read_number(window, "visible"), "window visible") == 0, "window starts hidden");
  require(ok(context.read_number(window, "width"), "window width") == 420, "window reports requested width");

  const auto column = ok(context.widget("column", window, ""), "create column");
  require(ok(context.read_number(column, "kind"), "column kind") == 1, "column reports kind 1");
  const auto label = ok(context.widget("label", column, "Hello"), "create label");
  const auto button = ok(context.widget("button", column, "Press"), "create button");
  const auto check = ok(context.widget("checkbox", column, "Check"), "create checkbox");
  const auto entry = ok(context.widget("entry", column, "abc"), "create entry");

  const auto text = context.read_text(label);
  require(ok(text, "read label") == 0 && text.text == "Hello", "label keeps its text");
  const auto typed = context.read_text(entry);
  require(ok(typed, "read entry") == 0 && typed.text == "abc", "entry reports its live text");

  const std::string unicode = "caf\xc3\xa9 \xe4\xb8\xad\xe6\x96\x87";
  ok(context.set_text(label, unicode), "set unicode label");
  const auto round_trip = context.read_text(label);
  require(ok(round_trip, "read unicode label") == 0 && round_trip.text == unicode,
          "label round-trips non-ASCII text");

  ok(context.set_number(window, "visible", 1), "present window");
  require(ok(context.read_number(window, "visible"), "window visible") == 1, "window reports visible");
  require(ok(context.read_number(label, "effective_visible"), "label effective visible") == 1,
          "a child of a shown window is effectively visible");
  require(ok(context.read_number(entry, "width"), "entry width") > 0, "provider allocates entry width");
  require(ok(context.read_number(entry, "height"), "entry height") > 0, "provider allocates entry height");

  require(next_event(context, window) == 0, "no event is queued before activation");
  ok(context.activate(button), "activate button");
  const auto snapshot = next_event(context, window);
  require(snapshot != 0, "button activation produces an event");
  require(ok(context.event_number(snapshot, "kind"), "event kind") == 1, "button activation is event kind 1");
  require(ok(context.event_number(snapshot, "target"), "event target") == button, "event targets the button");
  require(ok(context.event_number(snapshot, "window"), "event window") == window, "event names its window");
  ok(context.release_event(snapshot), "release button event");

  require(ok(context.read_number(check, "checked"), "checkbox state") == 0, "checkbox starts clear");
  ok(context.activate(check), "activate checkbox");
  const auto toggled = next_event(context, window);
  require(toggled != 0, "checkbox activation produces an event");
  require(ok(context.event_number(toggled, "kind"), "toggle kind") == 3, "checkbox toggle is event kind 3");
  require(ok(context.event_number(toggled, "checked"), "toggle checked") == 1, "toggle reports the new state");
  require(ok(context.read_number(check, "checked"), "checkbox state") == 1, "checkbox is now set");
  ok(context.release_event(toggled), "release toggle event");

  ok(context.set_text(entry, "quiet"), "set entry text");
  require(next_event(context, window) == 0, "programmatic set_text raises no event");
  ok(context.set_number(check, "checked", 0), "clear checkbox");
  require(next_event(context, window) == 0, "programmatic checked raises no event");

  ok(context.set_number(entry, "focus", 1), "focus entry");
  require(ok(context.read_number(entry, "focus"), "entry focus") == 1, "entry reports keyboard focus");

  ok(context.set_number(column, "sensitive", 0), "disable column");
  require(ok(context.read_number(button, "effective_sensitive"), "button effective sensitive") == 0,
          "a child of an insensitive container is insensitive");
  rejected(context.activate(button), "insensitive", "activate insensitive button");
  ok(context.set_number(column, "sensitive", 1), "re-enable column");

  const auto grid = ok(context.widget("grid", window, ""), "create grid");
  const auto first = ok(context.widget("label", grid, "one"), "create grid child one");
  const auto second = ok(context.widget("label", grid, "two"), "create grid child two");
  ok(context.grid_attach(grid, first, 0, 0, 1, 1), "attach first grid child");
  rejected(context.grid_attach(grid, second, 0, 0, 1, 1), "overlaps", "overlapping grid placement");
  ok(context.grid_attach(grid, second, 1, 0, 1, 1), "attach second grid child");

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

  const auto doomed = ok(context.widget("row", column, ""), "create row");
  const auto child = ok(context.widget("label", doomed, "gone"), "create row child");
  ok(context.destroy_widget(doomed), "destroy row");
  rejected(context.read_number(child, "kind"), "handle", "descendant handle is invalidated");
  require(ok(context.read_number(label, "kind"), "surviving label") == 4, "unrelated widgets survive");

  ok(context.close(window), "close window");
  rejected(context.read_number(label, "kind"), "handle", "closed window invalidates its children");
  rejected(context.close(window), "handle", "closing twice is an error");
}

// Real keyboard input, delivered in process. macOS requires accessibility
// trust to post events to another process, which a hosted runner does not
// grant, so the language-level contract cannot drive the interaction suite
// there; posting into this process needs no such permission, and the provider
// pump is still what has to route it.
void input_probe(bool verbose) {
  @autoreleasepool {
    ny::ui::Context context;
    const auto window = ok(context.window("NIL AppKit input probe", 420, 240), "create probe window");
    const auto column = ok(context.widget("column", window, ""), "create probe column");
    const auto entry = ok(context.widget("entry", column, ""), "create probe entry");
    const auto box = ok(context.widget("checkbox", column, "Enable setting"), "create probe checkbox");
    const auto button = ok(context.widget("button", column, "Finish probe"), "create probe button");
    ok(context.set_number(window, "visible", 1), "show probe window");
    ok(context.set_number(entry, "focus", 1), "focus probe entry");

    NSWindow *top = nil;
    for (NSWindow *candidate in NSApp.windows)
      if ([candidate.title isEqualToString:@"NIL AppKit input probe"]) { top = candidate; break; }
    require(top != nil, "probe window is discoverable among the application windows");
    // Key events reach the key window's responder chain, so the application
    // has to be active for AppKit to route them at all.
    [NSApp activateIgnoringOtherApps:YES];
    if (verbose)
      std::cout << "input probe: active=" << (NSApp.active ? 1 : 0)
                << " key=" << (top.keyWindow ? 1 : 0)
                << " responder=" << (top.firstResponder
                        ? [NSStringFromClass([top.firstResponder class]) UTF8String] : "none")
                << "\n";

    const auto post = [&](NSString *characters, unsigned short code) {
      for (NSEventType type : {NSEventTypeKeyDown, NSEventTypeKeyUp}) {
        NSEvent *event = [NSEvent keyEventWithType:type
                                          location:NSZeroPoint
                                     modifierFlags:0
                                         timestamp:[[NSProcessInfo processInfo] systemUptime]
                                      windowNumber:top.windowNumber
                                           context:nil
                                        characters:characters
                       charactersIgnoringModifiers:characters
                                         isARepeat:NO
                                           keyCode:code];
        if (event) [NSApp postEvent:event atStart:NO];
      }
    };
    for (NSString *character in @[@"M", @"a", @"t", @"r", @"i", @"x"]) post(character, 0);
    post(@"\t", 48);  // kVK_Tab
    post(@" ", 49);   // kVK_Space
    post(@"\t", 48);
    post(@" ", 49);

    bool saw_text = false, saw_toggle = false, saw_click = false;
    std::string observed;
    for (int attempt = 0; attempt < 400 && !saw_click; ++attempt) {
      const auto polled = context.poll(window);
      if (!polled) break;
      if (!polled.number) { [NSThread sleepForTimeInterval:0.01]; continue; }
      const auto kind = ok(context.event_number(polled.number, "kind"), "probe event kind");
      const auto target = ok(context.event_number(polled.number, "target"), "probe event target");
      if (kind == 2 && target == entry) {
        observed = context.event_text(polled.number).text;
        if (observed == "Matrix") saw_text = true;
      } else if (kind == 3 && target == box &&
                 ok(context.event_number(polled.number, "checked"), "probe checked") == 1) saw_toggle = true;
      else if (kind == 1 && target == button) saw_click = true;
      ok(context.release_event(polled.number), "release probe event");
    }
    if (verbose)
      std::cout << "input probe: text=" << saw_text << " toggle=" << saw_toggle
                << " click=" << saw_click << " entry=\"" << observed << "\"\n";
    ok(context.close(window), "close probe window");
    require(saw_text, "posted characters reach the focused entry");
    require(saw_toggle, "Tab then Space reaches the checkbox");
    require(saw_click, "Tab then Space reaches the button");
  }
}

} // namespace

int main(int argc, char **argv) {
  const std::string mode = argc > 1 ? argv[1] : "";
  require(ny::ui::available(), "macOS provider is always available");
  if (mode == "--provider-present") {
    std::cout << "UI runtime: " << checks << " checks passed (--provider-present)\n";
    return 0;
  }
  const bool desktop = window_server();
  if (mode == "--input-probe") {
    require(desktop, "--input-probe requires a window server session");
    input_probe(true);
    std::cout << "UI runtime: " << checks << " checks passed (--input-probe)\n";
    return 0;
  }
  if (mode == "--display") {
    require(desktop, "--display requires a window server session");
    contract();
    std::cout << "UI runtime: " << checks << " checks passed (--display)\n";
    return 0;
  }
  if (desktop) contract();
  else {
    ny::ui::Context context;
    rejected(context.window("no session", 320, 240), "window server",
             "window creation without a window server session");
  }
  std::cout << "UI runtime: " << checks << " checks passed ("
            << (desktop ? "desktop" : "no-desktop") << ")\n";
  return 0;
}
