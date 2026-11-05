// Private hosted prerequisite: only one verified child receives directed input.
// No global posting, permission requests, application enumeration or product UI.
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <libproc.h>
#include <pthread.h>
#include <sys/proc_info.h>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <string>

static constexpr char expectedText[] = "ny42";
static constexpr CGKeyCode keyCodes[] = {45, 16, 21, 19};
// XNU11417 proc_info_private.h API layout, also used by process_contract_support.py.
// Public SDKs need not ship the private header. A size mismatch fails compilation.
struct UniqueInfo { uint8_t uuid[16]; uint64_t unique, parent; int32_t version, parentVersion;
  uint64_t reserved2, reserved3; };
struct ProcessInfo { proc_bsdinfo bsd; UniqueInfo unique; };
static_assert(sizeof(proc_bsdinfo) == 136 && sizeof(UniqueInfo) == 56 && sizeof(ProcessInfo) == 192,
              "unsupported macOS process identity layout");

static void emit(NSDictionary *value) {
  NSError *error = nil;
  NSData *data = [NSJSONSerialization dataWithJSONObject:value options:NSJSONWritingSortedKeys error:&error];
  if (!data || error || data.length > 8192) std::abort();
  std::fwrite(data.bytes, 1, data.length, stdout); std::fputc('\n', stdout); std::fflush(stdout);
}
static bool number(const char *text, uint64_t &result) {
  if (!text || !*text || std::strlen(text) > 20) return false;
  for (const char *p = text; *p; ++p) if (*p < '0' || *p > '9') return false;
  errno = 0; char *end = nullptr; result = std::strtoull(text, &end, 10);
  return !errno && end && !*end;
}
static bool nonceValid(const char *s) {
  if (!s || std::strlen(s) != 32) return false;
  for (size_t i = 0; i < 32; ++i) if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
  return true;
}
static bool infoFor(pid_t pid, ProcessInfo &info) {
  return pid > 0 && proc_pidinfo(pid, 18, 1, &info, sizeof(info)) == sizeof(info) &&
      info.bsd.pbi_pid == static_cast<uint32_t>(pid) && info.unique.unique &&
      info.bsd.pbi_status >= 1 && info.bsd.pbi_status <= 4; // reject zombie/unknown
}
static bool canonical(const char *path, std::string &value) {
  char resolved[PATH_MAX]; if (!realpath(path, resolved)) return false; value = resolved; return true;
}
struct Target {
  pid_t pid = 0, parent = 0; uint64_t unique = 0, version = 0, device = 0, inode = 0, marker = 0;
  std::string executable, nonce;
  bool verify() const {
    ProcessInfo info{}; char path[PROC_PIDPATHINFO_MAXSIZE]{}; struct stat state{};
    std::string actual;
    return parent == getppid() && infoFor(pid, info) && info.bsd.pbi_ppid == static_cast<uint32_t>(parent) &&
      info.unique.unique == unique && static_cast<uint32_t>(info.unique.version) == version &&
      proc_pidpath(pid, path, sizeof(path)) > 0 && canonical(path, actual) && actual == executable &&
      stat(executable.c_str(), &state) == 0 && S_ISREG(state.st_mode) &&
      static_cast<uint64_t>(state.st_dev) == device && static_cast<uint64_t>(state.st_ino) == inode;
  }
};
static bool parseTarget(int argc, const char **argv, Target &target) {
  if (argc != 11 || !nonceValid(argv[9])) return false;
  uint64_t values[7]{};
  for (int i = 0; i < 6; ++i) if (!number(argv[i + 2], values[i])) return false;
  if (!number(argv[10], values[6]) || !values[0] || values[0] > INT32_MAX || !values[3] || values[3] > INT32_MAX ||
      values[2] > UINT32_MAX || !values[1] || !values[6] || values[6] > INT64_MAX) return false;
  target.pid = static_cast<pid_t>(values[0]); target.unique = values[1]; target.version = values[2];
  target.parent = static_cast<pid_t>(values[3]); target.device = values[4]; target.inode = values[5];
  target.marker = values[6]; target.nonce = argv[9];
  std::string own; char ownPath[PROC_PIDPATHINFO_MAXSIZE]{};
  return canonical(argv[8], target.executable) && target.executable == argv[8] &&
      proc_pidpath(getpid(), ownPath, sizeof(ownPath)) > 0 && canonical(ownPath, own) && own == target.executable;
}
static CFTypeRef attribute(AXUIElementRef element, CFStringRef key) {
  CFTypeRef value = nullptr;
  if (AXUIElementCopyAttributeValue(element, key, &value) != kAXErrorSuccess) return nullptr;
  return value;
}
static bool owner(AXUIElementRef element, pid_t pid) {
  pid_t actual = 0; return AXUIElementGetPid(element, &actual) == kAXErrorSuccess && actual == pid;
}
static bool stringAttribute(AXUIElementRef element, CFStringRef key, CFStringRef expected) {
  CFTypeRef value = attribute(element, key);
  bool ok = value && CFGetTypeID(value) == CFStringGetTypeID() && CFEqual(value, expected);
  if (value) CFRelease(value);
  return ok;
}
static bool trueAttribute(AXUIElementRef element, CFStringRef key) {
  CFTypeRef value = attribute(element, key); bool ok = value && CFEqual(value, kCFBooleanTrue);
  if (value) CFRelease(value);
  return ok;
}
static AXUIElementRef ownWindow(AXUIElementRef app, const Target &target) {
  CFTypeRef value = attribute(app, kAXWindowsAttribute);
  if (!value) return nullptr;
  AXUIElementRef result = nullptr;
  if (CFGetTypeID(value) == CFArrayGetTypeID() && CFArrayGetCount(static_cast<CFArrayRef>(value)) <= 8) {
    NSString *title = [@"Ny directed " stringByAppendingString:[NSString stringWithUTF8String:target.nonce.c_str()]];
    for (CFIndex i = 0; i < CFArrayGetCount(static_cast<CFArrayRef>(value)); ++i) {
      CFTypeRef item = CFArrayGetValueAtIndex(static_cast<CFArrayRef>(value), i);
      if (CFGetTypeID(item) != AXUIElementGetTypeID()) continue;
      auto window = static_cast<AXUIElementRef>(const_cast<void *>(item));
      if (owner(window, target.pid) && stringAttribute(window, kAXRoleAttribute, kAXWindowRole) &&
          stringAttribute(window, kAXTitleAttribute, (__bridge CFStringRef)title)) {
        if (result) { CFRelease(result); result = nullptr; break; }
        CFRetain(window); result = window;
      }
    }
  }
  CFRelease(value); return result;
}
static int driver(const char *mode, const Target &target) {
  bool identity = target.verify();
  const bool post = CGPreflightPostEventAccess(), ax = AXIsProcessTrusted();
  bool selected = false, focused = false, marked = false, pressed = false;
  unsigned posted = 0;
  AXUIElementRef app = nullptr, window = nullptr; CFTypeRef button = nullptr;
  if (identity && post && ax) {
    app = AXUIElementCreateApplication(target.pid);
    if (app && AXUIElementSetMessagingTimeout(app, 1.0f) == kAXErrorSuccess) window = ownWindow(app, target);
    selected = window != nullptr;
    if (selected) {
      focused = trueAttribute(window, kAXFocusedAttribute);
      if (!std::strcmp(mode, "keys") && focused) {
        CFTypeRef field = attribute(app, kAXFocusedUIElementAttribute);
        focused = field && CFGetTypeID(field) == AXUIElementGetTypeID() &&
          owner(static_cast<AXUIElementRef>(const_cast<void *>(field)), target.pid) &&
          stringAttribute(static_cast<AXUIElementRef>(const_cast<void *>(field)), kAXRoleAttribute, kAXTextFieldRole);
        if (field) CFRelease(field);
        marked = focused;
        for (unsigned i = 0; focused && i < 4; ++i) {
          for (unsigned down = 0; down < 2; ++down) {
            if (!target.verify()) { identity = false; break; }
            CGEventRef event = CGEventCreateKeyboardEvent(nullptr, keyCodes[i], down == 0);
            if (!event) { marked = false; break; }
            UniChar text = static_cast<UniChar>(expectedText[i]);
            CGEventKeyboardSetUnicodeString(event, 1, &text);
            CGEventSetFlags(event, 0);
            CGEventSetIntegerValueField(event, kCGEventSourceUserData, static_cast<int64_t>(target.marker));
            const bool exact = CGEventGetIntegerValueField(event, kCGEventSourceUserData) == static_cast<int64_t>(target.marker);
            marked = marked && exact;
            if (exact) { CGEventPostToPid(target.pid, event); ++posted; }
            CFRelease(event);
            if (!exact) break;
          }
          if (!identity || !marked) break;
        }
      } else if (!std::strcmp(mode, "close") && focused) {
        button = attribute(window, kAXCloseButtonAttribute);
        if (button && CFGetTypeID(button) == AXUIElementGetTypeID()) {
          auto element = static_cast<AXUIElementRef>(const_cast<void *>(button));
          if (owner(element, target.pid) && stringAttribute(element, kAXRoleAttribute, kAXButtonRole) && target.verify())
            pressed = AXUIElementPerformAction(element, kAXPressAction) == kAXErrorSuccess;
        }
      }
    }
  }
  if (button) CFRelease(button);
  if (window) CFRelease(window);
  if (app) CFRelease(app);
  const bool success = identity && post && ax && selected && focused &&
    (!std::strcmp(mode, "keys") ? marked && posted == 8 : pressed);
  emit(@{@"schema": @1, @"phase": [NSString stringWithUTF8String:mode], @"qualification": @NO,
      @"identity_verified": @(identity), @"post_event_access": @(post), @"ax_trusted": @(ax),
      @"own_window_selected": @(selected), @"focus_verified": @(focused), @"marker_readback": @(marked),
      @"events_posted": @(posted), @"close_action": @(pressed), @"success": @(success)});
  return success ? 0 : 2;
}

static uint64_t expectedMarker = 0;
static bool dispatching = false, unexpected = false, emittedTyped = false, closed = false;
static unsigned dispatchCount = 0, callbacks = 0, shouldClose = 0, didClose = 0;
static unsigned unmarkedKeyboard = 0, markedInvalid = 0;
static NSInteger windowNumber = 0;
static void stopApp(NSApplication *app) {
  [app stop:nil];
  [app postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint
    modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0] atStart:NO];
}
@interface DirectedApplication : NSApplication
@end
@implementation DirectedApplication
- (void)sendEvent:(NSEvent *)event {
  const bool keyboard = event.type == NSEventTypeKeyDown || event.type == NSEventTypeKeyUp;
  CGEventRef cg = keyboard ? event.CGEvent : nullptr;
  const bool marked = cg && static_cast<uint64_t>(CGEventGetIntegerValueField(cg, kCGEventSourceUserData)) == expectedMarker;
  const unsigned index = dispatchCount / 2;
  const bool valid = marked && index < 4 && windowNumber > 0 && event.windowNumber == windowNumber &&
    event.type == (dispatchCount % 2 ? NSEventTypeKeyUp : NSEventTypeKeyDown) &&
    event.keyCode == keyCodes[index] && event.modifierFlags == 0 &&
    [event.characters isEqualToString:[NSString stringWithFormat:@"%c", expectedText[index]]];
  if (keyboard && !marked && unmarkedKeyboard < 1000) ++unmarkedKeyboard;
  if (marked && !valid) { unexpected = true; if (markedInvalid < 1000) ++markedInvalid; }
  if (valid) ++dispatchCount;
  bool previous = dispatching; dispatching = valid && event.type == NSEventTypeKeyDown;
  @try { [super sendEvent:event]; } @finally { dispatching = previous; }
  // Publish only after the last real keyUp dispatch. The parent must not start
  // AX close while a previously queued keyboard event is still outstanding.
  if (dispatchCount == 8 && callbacks == 4 && !unexpected && !emittedTyped) {
    emittedTyped = true;
    emit(@{@"schema": @1, @"phase": @"typed", @"qualification": @NO, @"text": @"ny42",
      @"callbacks": @(callbacks), @"marker_dispatches": @(dispatchCount), @"during_marked_dispatch": @YES});
  }
}
@end
@interface DirectedDelegate : NSObject <NSWindowDelegate, NSTextFieldDelegate>
@end
@implementation DirectedDelegate
- (void)controlTextDidChange:(NSNotification *)notification {
  NSTextField *field = notification.object;
  ++callbacks;
  NSString *expected = callbacks <= 4 ? [NSString stringWithUTF8String:std::string(expectedText, callbacks).c_str()] : nil;
  if (!dispatching || !expected || ![field.stringValue isEqualToString:expected]) unexpected = true;
}
- (BOOL)windowShouldClose:(NSWindow *)sender {
  ++shouldClose;
  if (sender.windowNumber != windowNumber || !emittedTyped || unexpected) return NO;
  return YES;
}
- (void)windowWillClose:(NSNotification *)notification {
  NSWindow *window = notification.object;
  ++didClose; closed = window.windowNumber == windowNumber;
  stopApp(NSApp);
}
@end
static int target(const char *nonce, uint64_t marker) {
  expectedMarker = marker;
  bool ready = false, expired = false, exception = false, observer = false;
  NSApplication *app = nil; NSWindow *window = nil; NSTextField *field = nil;
  DirectedDelegate *delegate = nil; NSTimer *timer = nil;
  @try {
    if (!pthread_main_np()) @throw [NSException exceptionWithName:@"MainThread" reason:@"main thread required" userInfo:nil];
    app = [DirectedApplication sharedApplication];
    observer = [app isKindOfClass:DirectedApplication.class];
    if (!observer) @throw [NSException exceptionWithName:@"Observer" reason:@"observer class required" userInfo:nil];
    [app setActivationPolicy:NSApplicationActivationPolicyAccessory]; [app finishLaunching];
    window = [[NSWindow alloc] initWithContentRect:NSMakeRect(50, 50, 340, 100)
      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
    window.releasedWhenClosed = NO;
    window.title = [@"Ny directed " stringByAppendingString:[NSString stringWithUTF8String:nonce]];
    field = [[NSTextField alloc] initWithFrame:NSMakeRect(20, 30, 280, 30)];
    delegate = [[DirectedDelegate alloc] init]; window.delegate = delegate; field.delegate = delegate;
    [window.contentView addSubview:field]; [window makeKeyAndOrderFront:nil];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [app activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
    [window makeFirstResponder:field]; windowNumber = window.windowNumber;
    const auto started = std::chrono::steady_clock::now();
    __block bool reportedReady = false, timedOut = false;
    timer = [NSTimer scheduledTimerWithTimeInterval:0.025 repeats:YES block:^(NSTimer *) {
      bool focus = app.isRunning && app.isActive && window.isKeyWindow && window.isVisible &&
          field.currentEditor && window.firstResponder == field.currentEditor;
      if (!reportedReady && focus) {
        reportedReady = true;
        emit(@{@"schema": @1, @"phase": @"ready", @"qualification": @NO, @"pid": @(getpid()),
          @"window_number": @(windowNumber), @"active": @YES, @"key_window": @YES,
          @"field_editor": @YES, @"observer_subclass": @(observer), @"main_thread": @(pthread_main_np() != 0)});
      }
      if (std::chrono::steady_clock::now() - started > std::chrono::seconds(25)) {
        timedOut = true; stopApp(app);
      }
    }];
    [app run]; ready = reportedReady; expired = timedOut;
  } @catch (NSException *) { exception = true; }
  @finally {
    [timer invalidate];
    // Cleanup cannot manufacture an observed native AX close notification.
    window.delegate = nil; field.delegate = nil; [window orderOut:nil]; [window close];
  }
  const bool cleaned = !window || !window.isVisible;
  const bool success = ready && observer && !expired && !exception && !unexpected && emittedTyped &&
      dispatchCount == 8 && callbacks == 4 && closed && shouldClose == 1 && didClose == 1 && cleaned;
  emit(@{@"schema": @1, @"phase": @"final", @"qualification": @NO, @"ready": @(ready),
    @"marker_dispatches": @(dispatchCount), @"text_callbacks": @(callbacks), @"typed": @(emittedTyped),
    @"unmarked_keyboard_count": @(unmarkedKeyboard), @"invalid_marked_count": @(markedInvalid),
    @"unexpected_input": @(unexpected), @"window_should_close": @(shouldClose), @"window_did_close": @(didClose),
    @"native_close": @(closed), @"expired": @(expired), @"exception": @(exception),
    @"cleanup_verified": @(cleaned), @"success": @(success)});
  return success ? 0 : 2;
}
int main(int argc, const char **argv) {
  @autoreleasepool {
    if (argc == 4 && !std::strcmp(argv[1], "target")) {
      uint64_t marker = 0;
      if (nonceValid(argv[2]) && number(argv[3], marker) && marker && marker <= INT64_MAX) return target(argv[2], marker);
    } else if (argc > 1 && (!std::strcmp(argv[1], "keys") || !std::strcmp(argv[1], "close"))) {
      Target value; if (parseTarget(argc, argv, value)) return driver(argv[1], value);
    }
    std::fputs("invalid private directed-probe arguments\n", stderr); return 2;
  }
}
