/*
 * Copyright (C) 2008 - 2026 Execute Software. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
// Test-only in-process input for the unchanged Matrix interaction suite.
// This library is never installed. It neither requests Accessibility trust nor
// posts global/cross-process input. The provider's poll-driven main run loop
// invokes the timer; actual AppKit responders then generate provider events.
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

@interface NilAppKitInputDriver : NSObject
@property(nonatomic, strong) NSTimer *timer;
@property(nonatomic, copy) NSString *token;
@property(nonatomic, copy) NSString *directory;
@property(nonatomic, copy) NSString *route;
@property(nonatomic, copy) NSString *test;
@property(nonatomic, weak) NSWindow *window;
@property(nonatomic, weak) NSTextField *entry;
@property(nonatomic, weak) NSButton *checkbox;
@property(nonatomic, weak) NSButton *button;
@property(nonatomic) NSTimeInterval deadline;
@property(nonatomic) NSUInteger stage;
@property(nonatomic) NSUInteger keys;
@property(nonatomic) pid_t parent;
@property(nonatomic) BOOL driving;
- (void)tick:(NSTimer *)timer;
@end

// Keep the callback receiver alive through its final report, even when timer
// invalidation removes the run loop's last reference during that callback.
static NilAppKitInputDriver *active_driver;

@implementation NilAppKitInputDriver
- (void)finish:(NSString *)status reason:(NSString *)reason {
  [self.timer invalidate]; self.timer = nil;
  NSDictionary *record = @{@"schema": @"nil.appkit.input.v1", @"token": self.token,
      @"route": self.route, @"test": self.test ?: @"unknown", @"pid": @(getpid()),
      @"parentPid": @(self.parent), @"status": status, @"reason": reason,
      @"keys": @(self.keys), @"stage": @(self.stage),
      @"scope": @"owned-process AppKit responder input; no Accessibility or global input"};
  NSData *data = [NSJSONSerialization dataWithJSONObject:record options:NSJSONWritingSortedKeys error:nullptr];
  NSString *path = [self.directory stringByAppendingPathComponent:
      [NSString stringWithFormat:@"%@-%d.json", self.token, getpid()]];
  const int fd = open(path.fileSystemRepresentation, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (!data || fd < 0) { std::fputs("NIL AppKit input evidence creation failed\n", stderr); _exit(85); }
  const char *bytes = (const char *)data.bytes;
  std::size_t pending = data.length;
  while (pending) {
    const auto count = write(fd, bytes, pending);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) { close(fd); _exit(85); }
    bytes += count; pending -= static_cast<std::size_t>(count);
  }
  if (close(fd)) _exit(85);
  std::fputs("NIL_APPKIT_INPUT ", stderr);
  std::fwrite(data.bytes, 1, data.length, stderr);
  std::fputc('\n', stderr); std::fflush(stderr);
  if (![status isEqualToString:@"passed"]) _exit(85);
}
- (void)fail:(NSString *)reason { [self finish:@"failed" reason:reason]; }
- (BOOL)findControls:(NSView *)root {
  if ([root isKindOfClass:[NSTextField class]] && ((NSTextField *)root).editable) {
    if (self.entry) return NO;
    self.entry = (NSTextField *)root;
  } else if ([root isKindOfClass:[NSButton class]]) {
    NSButton *control = (NSButton *)root;
    if ([control.title isEqualToString:@"Enable setting"]) {
      if (self.checkbox) return NO;
      self.checkbox = control;
    } else if ([control.title isEqualToString:@"Finish probe"]) {
      if (self.button) return NO;
      self.button = control;
    } else return NO;
  }
  for (NSView *child in root.subviews) if (![self findControls:child]) return NO;
  return YES;
}
- (void)key:(NSString *)characters code:(unsigned short)code {
  NSWindow *window = self.window;
  if (!window || !window.visible) { [self fail:@"owned fixture window disappeared before input"]; return; }
  const auto event = ^NSEvent *(NSEventType type) {
    return [NSEvent keyEventWithType:type location:NSZeroPoint modifierFlags:0
        timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:window.windowNumber
        context:nil characters:characters charactersIgnoringModifiers:characters
        isARepeat:NO keyCode:code];
  };
  NSEvent *down = event(NSEventTypeKeyDown), *up = event(NSEventTypeKeyUp);
  if (!down || !up) { [self fail:@"could not construct owned-window key events"]; return; }
  // Queue key-up first so a control's synchronous key tracking can consume it;
  // direct key-down targets this verified NSWindow even if the app is inactive.
  // No activation policy, system focus, HID tap or other process is modified.
  [NSApp postEvent:up atStart:NO];
  self.driving = YES;
  [window sendEvent:down];
  self.driving = NO;
  ++self.keys;
}
- (void)tick:(NSTimer *)timer {
  (void)timer;
  @autoreleasepool {
    if (self.driving) return;
    if (![NSThread isMainThread]) { [self fail:@"input timer left the process main thread"]; return; }
    if (!self.test) {
      if (!NSApp) return; // Discovery/compiler processes never create fixture windows.
      NSWindow *found = nil;
      for (NSWindow *candidate in NSApp.windows) {
        if (!candidate.visible || !([candidate.title isEqualToString:@"NIL2 UI keyboard probe"] ||
                                   [candidate.title isEqualToString:@"NIL2 UI close probe"])) continue;
        if (found) { self.test = @"ambiguous"; [self fail:@"more than one owned fixture window"]; return; }
        found = candidate;
      }
      if (!found) return;
      self.window = found;
      self.deadline = NSProcessInfo.processInfo.systemUptime + 10;
      if ([found.title isEqualToString:@"NIL2 UI close probe"]) {
        self.test = @"close_request";
        [found performClose:nil];
        if (!found.visible) { [self fail:@"close request destroyed the live language handle"]; return; }
        self.stage = 1;
        [self finish:@"passed" reason:@"owned window-manager close action delivered"];
        return;
      }
      self.test = @"keyboard_events";
      if (![self findControls:found.contentView] || !self.entry || !self.checkbox || !self.button ||
          !self.entry.enabled || !self.checkbox.enabled || !self.button.enabled ||
          self.entry.currentEditor != found.firstResponder || self.entry.stringValue.length) {
        [self fail:@"keyboard fixture controls or initial language-requested focus did not match"]; return;
      }
      self.stage = 1;
      for (NSString *character in @[@"M", @"a", @"t", @"r", @"i", @"x"]) [self key:character code:0];
      return;
    }
    if (NSProcessInfo.processInfo.systemUptime > self.deadline) {
      [self fail:@"owned input phases exceeded ten seconds"]; return;
    }
    if (self.stage == 1) {
      if (![self.entry.stringValue isEqualToString:@"Matrix"]) return;
      if (![self.window makeFirstResponder:self.checkbox]) { [self fail:@"checkbox refused in-process focus"]; return; }
      self.stage = 2; [self key:@" " code:49];
    } else if (self.stage == 2) {
      if (self.checkbox.state != NSControlStateValueOn) return;
      if (![self.window makeFirstResponder:self.button]) { [self fail:@"button refused in-process focus"]; return; }
      self.stage = 3; [self key:@" " code:49];
      [self finish:@"passed" reason:@"owned entry text and checkbox state observed; button key delivered"];
    }
  }
}
@end

__attribute__((constructor)) static void nil_appkit_input_start() {
  const char *token = std::getenv("NIL_TEST_APPKIT_INPUT_TOKEN");
  if (!token) return;
  const char *directory = std::getenv("NIL_TEST_APPKIT_INPUT_DIRECTORY");
  const char *route = std::getenv("NIL_TEST_APPKIT_INPUT_ROUTE");
  if (std::strlen(token) != 32 || std::strspn(token, "0123456789abcdef") != 32 ||
      !directory || *directory != '/' || !route || (std::strcmp(route, "vm") && std::strcmp(route, "native"))) _exit(85);
  @autoreleasepool {
    NilAppKitInputDriver *driver = [[NilAppKitInputDriver alloc] init];
    active_driver = driver;
    driver.token = [NSString stringWithUTF8String:token];
    driver.directory = [NSString stringWithUTF8String:directory];
    driver.route = [NSString stringWithUTF8String:route];
    driver.parent = getppid();
    driver.timer = [NSTimer timerWithTimeInterval:0.01 target:driver selector:@selector(tick:)
                                       userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:driver.timer forMode:NSDefaultRunLoopMode];
  }
}
