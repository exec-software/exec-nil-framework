// Host feasibility only: one owned AppKit window, no external/global input.
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <pthread.h>
#include <cstdio>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

// BEGIN standalone timestamp controls (also exercised before native UI setup).
struct KeyTimestamp {
  double seconds;
  int64_t window;
  unsigned type, code;
  uint64_t modifiers;
};

static const char *timestampMatch(const KeyTimestamp &event, const KeyTimestamp &expected) {
  if (!std::isfinite(event.seconds) || !std::isfinite(expected.seconds) ||
      event.seconds <= 0 || expected.seconds <= 0 || expected.window <= 0 || event.window != expected.window ||
      event.type != 10 || expected.type != 10 || event.code != 45 || expected.code != 45 ||
      event.modifiers != 0 || expected.modifiers != 0) return "none";
  if (event.seconds == expected.seconds) return "timestamp_exact";
  if (std::abs(event.seconds - expected.seconds) <= 1e-9 &&
      std::nextafter(expected.seconds, event.seconds) == event.seconds) return "timestamp_adjacent";
  return "none";
}

struct TimestampControls { size_t count; bool passed; };
static TimestampControls checkTimestamps() {
  const KeyTimestamp base{64.0, 7, 10, 45, 0};
  struct Case { KeyTimestamp event, expected; const char *kind; };
  std::vector<Case> cases{{base, base, "timestamp_exact"}};
  for (const double direction : {0.0, std::numeric_limits<double>::infinity()}) {
    auto event = base;
    event.seconds = std::nextafter(base.seconds, direction);
    cases.push_back({event, base, "timestamp_adjacent"});
    event.seconds = std::nextafter(event.seconds, direction);
    cases.push_back({event, base, "none"});
  }
  for (int field = 0; field != 4; ++field) {
    auto event = base; event.seconds = std::nextafter(base.seconds, 0.0);
    if (field == 0) event.window = 8;
    if (field == 1) event.type = 11;
    if (field == 2) event.code = 0;
    if (field == 3) event.modifiers = 1;
    cases.push_back({event, base, "none"});
  }
  for (const double value : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
    auto invalid = base; invalid.seconds = value;
    cases.push_back({invalid, base, "none"});
    cases.push_back({base, invalid, "none"});
  }
  auto zeroWindow = base; zeroWindow.window = 0;
  cases.push_back({zeroWindow, zeroWindow, "none"});
  auto large = base; large.seconds = 16777216.0;
  auto adjacentLarge = large; adjacentLarge.seconds = std::nextafter(large.seconds, INFINITY);
  cases.push_back({adjacentLarge, large, "none"}); // One ULP now exceeds the absolute 1 ns cap.
  bool passed = true;
  for (const auto &test : cases)
    passed = passed && std::strcmp(timestampMatch(test.event, test.expected), test.kind) == 0;
  return {cases.size(), passed};
}
// END standalone timestamp controls.

static int64_t privateMarker = 0;
static bool markerRetained = false;

static const char *syntheticMatchKind(NSEvent *event, NSEvent *expected) {
  if (!expected) return "none";
  if (event == expected) return "object";
  const char *timeKind = timestampMatch(
      {event.timestamp, event.windowNumber, static_cast<unsigned>(event.type), event.keyCode, event.modifierFlags},
      {expected.timestamp, expected.windowNumber, static_cast<unsigned>(expected.type), expected.keyCode, expected.modifierFlags});
  if (std::strcmp(timeKind, "none") != 0) return timeKind;
  CGEventRef quartz = markerRetained ? event.CGEvent : nullptr;
  return quartz && CGEventGetIntegerValueField(quartz, kCGEventSourceUserData) == privateMarker ? "marker" : "none";
}

static bool sameSyntheticEvent(NSEvent *event, NSEvent *expected) {
  return std::strcmp(syntheticMatchKind(event, expected), "none") != 0;
}

static NSDictionary *observeIdentity(NSEvent *event, NSEvent *expected) {
  const char *kind = syntheticMatchKind(event, expected);
  if (std::strcmp(kind, "none") == 0) return @{};
  char queued[64], received[64];
  std::snprintf(queued, sizeof queued, "%a", expected.timestamp);
  std::snprintf(received, sizeof received, "%a", event.timestamp);
  return @{@"kind":[NSString stringWithUTF8String:kind],
      @"queued_timestamp_hex":[NSString stringWithUTF8String:queued],
      @"observed_timestamp_hex":[NSString stringWithUTF8String:received]};
}

static NSDictionary *observeSyntheticEvent(NSEvent *event, NSEvent *expected) {
  // Character data is retained only for the owned event or a copy identified by
  // its bounded timestamp conversion or a marker verified on the queued NSEvent.
  if (!sameSyntheticEvent(event, expected)) return @{@"synthetic_identity_match":@NO};
  NSString *characters = event.characters ?: @"";
  NSString *unmodified = event.charactersIgnoringModifiers ?: @"";
  const bool bounded = characters.length <= 8 && unmodified.length <= 8;
  return @{@"synthetic_identity_match":@YES, @"strings_bounded":@(bounded),
      @"window_number":@(event.windowNumber), @"key_code":@(event.keyCode),
      @"type":@(event.type), @"modifier_flags":@(event.modifierFlags),
      @"characters":bounded ? characters : @"", @"characters_ignoring_modifiers":bounded ? unmodified : @""};
}

static bool expectedKey(NSEvent *event, NSEvent *expected, NSWindow *window) {
  return sameSyntheticEvent(event, expected) && event.window == window &&
      event.type == NSEventTypeKeyDown && event.keyCode == 45 && event.modifierFlags == 0 &&
      [event.characters isEqualToString:@"n"] && [event.charactersIgnoringModifiers isEqualToString:@"n"];
}

static void observeUnmatched(NSMutableArray *records, NSEvent *event, NSEvent *expected) {
  if (records.count >= 4) return;
  double delta = expected ? event.timestamp - expected.timestamp : 0;
  const bool clamped = !std::isfinite(delta) || delta < -3600 || delta > 3600;
  if (clamped) delta = 0;
  [records addObject:@{@"published":@(expected != nil), @"type":@(event.type),
      @"window_number":@(event.windowNumber), @"key_code":@(event.keyCode),
      @"has_reference_time":@(expected != nil), @"time_delta_seconds":@(delta), @"time_delta_clamped":@(clamped)}];
}

@interface NyProbeApplication : NSApplication
@property(nonatomic, strong) NSEvent *expectedEvent;
@property(nonatomic, weak) NSWindow *expectedWindow;
@property(nonatomic, strong) NSDictionary *observation;
@property(nonatomic, strong) NSDictionary *identity;
@property(nonatomic, strong) NSMutableArray *unmatched;
@property(nonatomic) NSUInteger dispatched;
@property(nonatomic) BOOL dispatchingOwned;
@end
@implementation NyProbeApplication
- (void)sendEvent:(NSEvent *)event {
  const BOOL previous = self.dispatchingOwned;
  self.dispatchingOwned = event.type == NSEventTypeKeyDown && sameSyntheticEvent(event, self.expectedEvent);
  @try {
    if (event.type == NSEventTypeKeyDown) {
      if (self.dispatchingOwned) {
        if (!self.observation) {
          self.observation = observeSyntheticEvent(event, self.expectedEvent);
          self.identity = observeIdentity(event, self.expectedEvent);
        }
        if (expectedKey(event, self.expectedEvent, self.expectedWindow)) self.dispatched += 1;
      } else observeUnmatched(self.unmatched, event, self.expectedEvent);
    }
  } @finally {
    // Observe the real run-loop dispatch; never replace or bypass AppKit delivery.
    @try { [super sendEvent:event]; }
    @finally { self.dispatchingOwned = previous; }
  }
}
@end

@interface NyProbeView : NSView
@property(nonatomic) NSUInteger received;
@property(nonatomic) BOOL wrongTarget;
@property(nonatomic, strong) NSEvent *expectedEvent;
@property(nonatomic, strong) NSDictionary *observation;
@property(nonatomic, strong) NSDictionary *identity;
@property(nonatomic, strong) NSMutableArray *unmatched;
@property(nonatomic, weak) NyProbeApplication *application;
@property(nonatomic) BOOL receivedDuringDispatch;
@end
@implementation NyProbeView
- (BOOL)acceptsFirstResponder { return YES; }
- (void)keyDown:(NSEvent *)event {
  if (!sameSyntheticEvent(event, self.expectedEvent)) {
    observeUnmatched(self.unmatched, event, self.expectedEvent);
    [super keyDown:event];
    return;
  }
  if (!self.observation) {
    self.observation = observeSyntheticEvent(event, self.expectedEvent);
    self.identity = observeIdentity(event, self.expectedEvent);
  }
  self.receivedDuringDispatch = self.application.dispatchingOwned && self.application.dispatched == 1;
  if (!self.receivedDuringDispatch || !expectedKey(event, self.expectedEvent, self.window))
    self.wrongTarget = YES;
  else
    self.received += 1;
}
@end

@interface NyProbeDelegate : NSObject <NSWindowDelegate>
@property(nonatomic) NSUInteger closeRequests;
@property(nonatomic) NSUInteger closed;
@end
@implementation NyProbeDelegate
- (BOOL)windowShouldClose:(NSWindow *)sender {
  (void)sender;
  self.closeRequests += 1;
  return YES;
}
- (void)windowWillClose:(NSNotification *)notification {
  (void)notification;
  self.closed += 1;
}
@end

static void stopOwnedLoop(NSApplication *app) {
  [app stop:nil];
  // stop: from a timer needs an event to wake NSApplication's event fetch.
  // This wake stays in the same application's queue and targets no window.
  NSEvent *wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint
      modifierFlags:0 timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:0
      context:nil subtype:0 data1:0 data2:0];
  [app postEvent:wake atStart:NO];
}

static void emit(NSDictionary *value) {
  NSData *data = [NSJSONSerialization dataWithJSONObject:value options:0 error:nullptr];
  if (data && data.length <= 8192) {
    std::fwrite(data.bytes, 1, data.length, stdout);
    std::fputc('\n', stdout);
  }
}

int main() {
  @autoreleasepool {
    const auto timestampControls = checkTimestamps();
    const bool mainThread = pthread_main_np() != 0;
    const bool postAccess = CGPreflightPostEventAccess();
    const bool axTrusted = AXIsProcessTrusted(); // Query only; never prompts.
    CFDictionaryRef session = CGSessionCopyCurrentDictionary();
    const bool hasSession = session != nullptr;
    const bool onConsole = session && CFDictionaryGetValue(session, kCGSessionOnConsoleKey) == kCFBooleanTrue;
    const bool loginDone = session && CFDictionaryGetValue(session, kCGSessionLoginDoneKey) == kCFBooleanTrue;
    if (session) CFRelease(session);
    NSMutableDictionary *result = [@{@"schema":@1, @"qualification":@NO, @"main_thread":@(mainThread),
        @"session":@(hasSession), @"on_console":@(onConsole), @"login_done":@(loginDone),
        @"post_event_access":@(postAccess), @"ax_trusted":@(axTrusted),
        @"timestamp_controls_passed":@(timestampControls.passed), @"timestamp_control_count":@(timestampControls.count),
        @"window_created":@NO, @"cleanup_verified":@YES} mutableCopy];
    if (!timestampControls.passed || !mainThread || !hasSession || !loginDone) { emit(result); return 2; }
    NSWindow *window = nil;
    NyProbeView *view = nil;
    NyProbeDelegate *delegate = nil;
    NSTimer *timer = nil;
    NyProbeApplication *app = nil;
    id monitor = nil;
    __block bool queued = false, ready = false, closeAttempted = false;
    __block NSUInteger monitorCount = 0;
    bool visible = false, delivered = false, closeDelivered = false, cleaned = false, exception = false;
    @try {
      NSApplication *shared = [NyProbeApplication sharedApplication];
      result[@"observer_subclass"] = @([shared isKindOfClass:[NyProbeApplication class]]);
      if (![shared isKindOfClass:[NyProbeApplication class]])
        @throw [NSException exceptionWithName:@"NyProbeApplicationIdentity" reason:@"Owned dispatch observer not instantiated" userInfo:nil];
      app = (NyProbeApplication *)shared;
      app.unmatched = [NSMutableArray array];
      [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
      [app finishLaunching];
      window = [[NSWindow alloc] initWithContentRect:NSMakeRect(32, 32, 220, 100)
           styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
           backing:NSBackingStoreBuffered defer:NO];
      [window setReleasedWhenClosed:NO];
      window.title = @"Ny private host feasibility probe";
      view = [[NyProbeView alloc] initWithFrame:NSMakeRect(0, 0, 220, 100)];
      view.unmatched = [NSMutableArray array];
      view.application = app;
      app.expectedWindow = window;
      delegate = [[NyProbeDelegate alloc] init];
      window.delegate = delegate;
      window.contentView = view;
      [window makeKeyAndOrderFront:nil];
      [window makeFirstResponder:view];
      [window displayIfNeeded];
      visible = window.isVisible && window.windowNumber > 0;
      result[@"before_run_active"] = @(app.isActive);
      result[@"before_run_running"] = @(app.isRunning);
      result[@"before_run_key_window"] = @(window.isKeyWindow);
      result[@"before_run_first_responder"] = @(window.firstResponder == view);
      // Observe only this application's dispatch of our exact private event.
      // Return every event unchanged; this is not a global event monitor.
      monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent *(NSEvent *event) {
        if (sameSyntheticEvent(event, view.expectedEvent)) {
          if (expectedKey(event, view.expectedEvent, window)) monitorCount += 1;
        }
        return event;
      }];
      const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(6);
      timer = [NSTimer timerWithTimeInterval:0.01 repeats:YES block:^(NSTimer *sender) {
        if (!queued && app.isActive && app.isRunning && window.isKeyWindow && window.firstResponder == view) {
          ready = true;
          NSEvent *event = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSMakePoint(20, 20)
              modifierFlags:0 timestamp:NSProcessInfo.processInfo.systemUptime
              windowNumber:window.windowNumber context:nil characters:@"n"
              charactersIgnoringModifiers:@"n" isARepeat:NO keyCode:45]; // ANSI N
          queued = event && event.window == window;
          if (queued) {
            uint64_t randomMarker = 0;
            arc4random_buf(&randomMarker, sizeof randomMarker);
            privateMarker = static_cast<int64_t>(randomMarker & INT64_MAX);
            if (!privateMarker) privateMarker = 1;
            CGEventRef quartz = event.CGEvent;
            if (quartz) {
              CGEventSetIntegerValueField(quartz, kCGEventSourceUserData, privateMarker);
              CGEventRef observed = event.CGEvent;
              markerRetained = observed && CGEventGetIntegerValueField(observed, kCGEventSourceUserData) == privateMarker;
            }
            view.expectedEvent = event;
            app.expectedEvent = event;
            result[@"queued_event"] = observeSyntheticEvent(event, event);
            result[@"queued_identity"] = observeIdentity(event, event);
            [app postEvent:event atStart:NO];
          }
        }
        if (view.received || std::chrono::steady_clock::now() >= end) {
          closeAttempted = true;
          [window performClose:nil];
          [sender invalidate];
          stopOwnedLoop(app);
        }
      }];
      [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];
      // Match the GTK4.18 Quartz backend's activation of an unbundled app.
      // This private diagnostic owns the only window it activates.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      [app activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
      [app run];
      result[@"after_run_active"] = @(app.isActive);
      result[@"after_run_running"] = @(app.isRunning);
      result[@"dispatch_count"] = @(app.dispatched);
      result[@"local_monitor_count"] = @(monitorCount);
      result[@"source_marker_retained"] = @(markerRetained);
      result[@"view_during_owned_dispatch"] = @(view.receivedDuringDispatch);
      result[@"view_delivery_count"] = @(view.received);
      result[@"wrong_target"] = @(view.wrongTarget);
      result[@"app_event"] = app.observation ?: @{};
      result[@"view_event"] = view.observation ?: @{};
      result[@"app_identity"] = app.identity ?: @{};
      result[@"view_identity"] = view.identity ?: @{};
      result[@"app_unmatched"] = app.unmatched;
      result[@"view_unmatched"] = view.unmatched;
      delivered = app.dispatched == 1 && view.received == 1 && view.receivedDuringDispatch && !view.wrongTarget;
      closeDelivered = closeAttempted && delegate.closeRequests == 1 && delegate.closed == 1 && !window.isVisible;
    } @catch (NSException *error) {
      (void)error;
      exception = true;
    } @finally {
      [timer invalidate];
      if (monitor) [NSEvent removeMonitor:monitor];
      view.expectedEvent = nil;
      app.expectedEvent = nil;
      app.expectedWindow = nil;
      if (window) {
        [window orderOut:nil];
        if (window.isVisible || delegate.closed == 0) [window close];
        cleaned = !window.isVisible;
        window.delegate = nil;
      } else cleaned = true;
    }
    [result addEntriesFromDictionary:@{@"window_created":@(window != nil), @"visible":@(visible),
        @"event_ready":@(ready), @"event_queued":@(queued), @"own_event_delivered":@(delivered),
        @"own_close_delivered":@(closeDelivered), @"exception":@(exception), @"cleanup_verified":@(cleaned)}];
    emit(result);
    return visible && ready && queued && delivered && closeDelivered && cleaned && !exception ? 0 : 2;
  }
}
