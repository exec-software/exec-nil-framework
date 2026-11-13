/* Checked provider ownership/ABI controls, no desktop. SPDX-License-Identifier: MIT */
#include "webview_runtime.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
extern "C" std::int64_t nilwebview_fixture_live_contexts();
extern "C" std::int64_t nilwebview_fixture_releases();
namespace {
int checks = 0;
void require(bool condition, const char *label) {
  ++checks;
  if (!condition) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
}
}
int main(int argc, char **argv) {
  if (argc != 3) return 2;
  using ny::webview::Context;
  {
    Context a, b;
    require(!a.window("unset", 640, 480), "unconfigured context");
    require(!a.configure("relative-provider"), "relative provider refused");
    require(!a.configure(argv[2]), "incompatible ABI refused");
    auto configured = a.configure(argv[1]);
    require(configured && configured.number == 0, "zero capabilities is successful configuration");
    require(bool(a.configure(argv[1])), "same provider configuration is idempotent");
    require(!a.configure(argv[2]), "cannot switch an active provider");
    require(bool(b.configure(argv[1])), "independent context");
    require(nilwebview_fixture_live_contexts() == 2, "exact live contexts");
    require(!a.window("bad",0,480), "zero dimension rejected");
    require(!a.window(std::string("x\0y",3),640,480), "NUL title rejected");
    require(!a.window(std::string("\xc0\xaf",2),640,480), "invalid UTF-8 title rejected");
    auto wa = a.window("first",640,480), wb = b.window("second",640,480);
    require(wa && wb && wa.number != wb.number, "context handles distinct despite repeated native handles");
    require(!a.show(wb.number) && !b.show(wa.number), "cross-context window handles refused");
    bool wrong_thread = false;
    std::thread other([&] { wrong_thread = !a.show(wa.number); }); other.join();
    require(wrong_thread, "foreign thread rejected");
    require(bool(a.show(wa.number)), "same-thread operation");
    auto ready = a.poll(wa.number);
    require(ready && ready.number > 0, "ready copied");
    require(a.event_number(ready.number,"window").number == wa.number, "native window translated");
    require(a.event_number(ready.number,"kind").number == 1, "event kind preserved");
    require(!a.event_number(ready.number,"unknown"), "unknown number field refused");
    require(!a.event_text(ready.number,"unknown"), "unknown text field refused");
    require(!b.event_number(ready.number,"kind"), "cross-context event handle refused");
    require(nilwebview_fixture_releases() == 1, "provider event released after copy");
    require(a.poll(wa.number).number == 0, "empty event queue");
    require(bool(a.release_event(ready.number)), "copied event released");
    require(!a.release_event(ready.number), "event cannot be released twice");
    require(!a.navigate(wa.number,std::string("x\0y",3)), "NUL operation input rejected");
    auto script = a.evaluate_function(wa.number,"return 42;");
    require(script && script.number == 32, "accepted operation identity");
    auto completed = a.poll(wa.number);
    require(completed && completed.number > 0, "script event copied");
    require(a.event_number(completed.number,"operation").number == script.number, "completion identity preserved");
    require(a.event_text(completed.number,"payload_json").text == "{\"answer\":42}", "snapshot survives provider storage mutation");
    require(bool(a.close(wa.number)), "window closed");
    require(!a.event_text(completed.number,"payload_json"), "close invalidates retained snapshots");
    require(!a.close(wa.number) && !a.show(wa.number), "stale window refused");
    require(bool(b.show(wb.number)), "unrelated context remains live");
    a.clear(); b.clear();
    require(nilwebview_fixture_live_contexts() == 0, "explicit context teardown");
    require(bool(a.configure(argv[1])), "context can be reused after terminal cleanup");
    auto w = a.window("invalid-result",640,480);
    auto first = a.poll(w.number); a.release_event(first.number);
    require(bool(a.evaluate_function(w.number,"bad-json")), "synthetic malformed result accepted asynchronously");
    const auto before = nilwebview_fixture_releases();
    require(!a.poll(w.number), "invalid provider JSON refused");
    require(nilwebview_fixture_releases() == before+1, "malformed event released on error");
    require(!a.show(w.number), "provider protocol fault remains latched");
    a.clear();
    require(bool(a.configure(argv[1])), "teardown resets protocol fault");
    w = a.window("bad-text",640,480); first = a.poll(w.number); a.release_event(first.number);
    a.evaluate_function(w.number,"bad-text");
    require(!a.poll(w.number), "invalid provider UTF-8 refused");
    for (const auto *invalid : {"bad-origin", "bad-url", "bad-frame", "bad-message", "deep65"}) {
      a.clear(); a.configure(argv[1]); w = a.window("invalid-event",640,480);
      first = a.poll(w.number); a.release_event(first.number);
      a.evaluate_function(w.number,invalid);
      const auto released_before = nilwebview_fixture_releases();
      require(!a.poll(w.number), "inconsistent provenance, event kind or depth refused");
      require(nilwebview_fixture_releases() == released_before+1, "invalid snapshot always released");
    }
    for (const auto *valid : {"surrogate", "deep33", "deep64"}) {
      a.clear(); a.configure(argv[1]); w = a.window("JSON-stringify-result",640,480);
      first = a.poll(w.number); a.release_event(first.number);
      a.evaluate_function(w.number,valid);
      auto found = a.poll(w.number);
      require(found && found.number > 0, "bounded JSON.stringify text preserved");
      if (std::string(valid) == "surrogate")
        require(a.event_text(found.number,"payload_json").text == "\"\\ud800\"", "escaped lone surrogate remains serialized text");
    }
  }
  require(nilwebview_fixture_live_contexts() == 0, "destructor cleanup");
  std::cout << checks << " WebView runtime checks passed\n";
}
