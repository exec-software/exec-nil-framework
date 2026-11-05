// Host feasibility only: one owned AppKit window, no external/global input.
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <pthread.h>
#include <cstdio>
#include <chrono>

@interface NyProbeView : NSView
@property(nonatomic) NSUInteger received;
@property(nonatomic) BOOL wrongTarget;
@end
@implementation NyProbeView
- (BOOL)acceptsFirstResponder { return YES; }
- (void)keyDown:(NSEvent *)event {
  if (event.window != self.window || ![event.characters isEqualToString:@"Ny"])
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

static void pump(NSApplication *app) {
  NSEvent *event = [app nextEventMatchingMask:NSEventMaskAny
                                   untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]
                                      inMode:NSDefaultRunLoopMode dequeue:YES];
  if (event) [app sendEvent:event];
  [app updateWindows];
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
    if (!mainThread || !hasSession || !loginDone) {
      std::printf("{\"schema\":1,\"qualification\":false,\"main_thread\":%s,\"session\":%s,\"on_console\":%s,\"login_done\":%s,\"post_event_access\":%s,\"ax_trusted\":%s,\"window_created\":false,\"cleanup_verified\":true}\n",
          mainThread?"true":"false", hasSession?"true":"false", onConsole?"true":"false", loginDone?"true":"false", postAccess?"true":"false", axTrusted?"true":"false");
      return 2;
    }
    NSWindow *window = nil;
    NyProbeView *view = nil;
    NyProbeDelegate *delegate = nil;
    bool visible = false, queued = false, delivered = false, closeDelivered = false, cleaned = false;
    bool exception = false;
    @try {
      NSApplication *app = [NSApplication sharedApplication];
      [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
      [app finishLaunching];
      window = [[NSWindow alloc] initWithContentRect:NSMakeRect(32, 32, 220, 100)
           styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
           backing:NSBackingStoreBuffered defer:NO];
      [window setReleasedWhenClosed:NO];
      window.title = @"Ny private host feasibility probe";
      view = [[NyProbeView alloc] initWithFrame:NSMakeRect(0, 0, 220, 100)];
      delegate = [[NyProbeDelegate alloc] init];
      window.delegate = delegate;
      window.contentView = view;
      [window makeKeyAndOrderFront:nil];
      [window makeFirstResponder:view];
      [window displayIfNeeded];
      visible = window.isVisible && window.windowNumber > 0;
      // This event stays in this NSApplication's queue. No CGEventPost,
      // CGEventPostToPid, Accessibility action, or system input injection.
      if (visible && window.firstResponder == view) {
        NSEvent *event = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSMakePoint(20, 20)
            modifierFlags:0 timestamp:NSProcessInfo.processInfo.systemUptime
            windowNumber:window.windowNumber context:nil characters:@"Ny"
            charactersIgnoringModifiers:@"Ny" isARepeat:NO keyCode:0];
        queued = event && event.window == window;
        if (queued) [app postEvent:event atStart:NO];
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (view.received == 0 && !view.wrongTarget && std::chrono::steady_clock::now() < end) pump(app);
        delivered = view.received == 1 && !view.wrongTarget;
      }
      [window performClose:nil];
      const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
      while (delegate.closed == 0 && std::chrono::steady_clock::now() < end) pump(app);
      closeDelivered = delegate.closeRequests == 1 && delegate.closed == 1 && !window.isVisible;
    } @catch (NSException *error) {
      (void)error; // Do not dump unrelated AppKit/session details.
      exception = true;
    } @finally {
      if (window) {
        [window orderOut:nil];
        if (window.isVisible || delegate.closed == 0) [window close];
        cleaned = !window.isVisible;
        window.delegate = nil;
      } else cleaned = true;
    }
    std::printf("{\"schema\":1,\"qualification\":false,\"main_thread\":true,\"session\":true,\"on_console\":%s,\"login_done\":true,\"post_event_access\":%s,\"ax_trusted\":%s,\"window_created\":%s,\"visible\":%s,\"event_queued\":%s,\"own_event_delivered\":%s,\"own_close_delivered\":%s,\"exception\":%s,\"cleanup_verified\":%s}\n",
        onConsole?"true":"false", postAccess?"true":"false", axTrusted?"true":"false", window?"true":"false", visible?"true":"false", queued?"true":"false", delivered?"true":"false", closeDelivered?"true":"false", exception?"true":"false", cleaned?"true":"false");
    return visible && queued && delivered && closeDelivered && cleaned && !exception ? 0 : 2;
  }
}
