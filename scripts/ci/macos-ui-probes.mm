// Host feasibility only: one owned AppKit window, no external/global input.
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <pthread.h>
#include <cstdio>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>

static int64_t privateMarker = 0;
static bool markerRetained = false;

static bool sameSyntheticEvent(NSEvent *event, NSEvent *expected) {
  if (!expected) return false;
  if (event == expected || event.timestamp == expected.timestamp) return true;
  CGEventRef quartz = markerRetained ? event.CGEvent : nullptr;
  return quartz && CGEventGetIntegerValueField(quartz, kCGEventSourceUserData) == privateMarker;
}

static NSDictionary *observeSyntheticEvent(NSEvent *event, NSEvent *expected) {
  // Character data is retained only for the owned event or a copy identified by
  // its exact timestamp or the private marker verified on the queued NSEvent.
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
        if (!self.observation) self.observation = observeSyntheticEvent(event, self.expectedEvent);
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
  if (!self.observation) self.observation = observeSyntheticEvent(event, self.expectedEvent);
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
        @"window_created":@NO, @"cleanup_verified":@YES} mutableCopy];
    if (!mainThread || !hasSession || !loginDone) { emit(result); return 2; }
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
