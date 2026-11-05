// Private GTK directed prerequisite: only one verified child receives input.
// No global posting, permission requests, application enumeration or product UI.
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <gtk/gtk.h>
#include <gdk/macos/gdkmacos.h>
#include <cmath>
#include <thread>
#include <dlfcn.h>
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

static constexpr char expectedText[] = "Matrix\t \t ";
static constexpr CGKeyCode keyCodes[] = {46, 0, 17, 15, 34, 7, 48, 49, 48, 49};
static constexpr guint keyValues[] = {GDK_KEY_M, GDK_KEY_a, GDK_KEY_t, GDK_KEY_r, GDK_KEY_i, GDK_KEY_x, GDK_KEY_Tab, GDK_KEY_space, GDK_KEY_Tab, GDK_KEY_space};
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
static NSString *loadedOrigin(const void *symbol) {
  Dl_info info{}; std::string path;
  if (!dladdr(symbol, &info) || !info.dli_fname || !canonical(info.dli_fname, path) || path.size() > 4096)
    @throw [NSException exceptionWithName:@"Origin" reason:@"file-backed provider origin required" userInfo:nil];
  return [NSString stringWithUTF8String:path.c_str()];
}
struct Target {
  pid_t pid = 0, parent = 0; uint64_t unique = 0, version = 0, device = 0, inode = 0, baseSeconds = 0;
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
      values[2] > UINT32_MAX || !values[1] || !values[6] || values[6] > (uint64_t{1} << 32)) return false;
  target.pid = static_cast<pid_t>(values[0]); target.unique = values[1]; target.version = values[2];
  target.parent = static_cast<pid_t>(values[3]); target.device = values[4]; target.inode = values[5];
  target.baseSeconds = values[6]; target.nonce = argv[9];
  // Parent supplies independently sealed target and driver paths. These need
  // not be the same program; only its retained direct child may be targeted.
  return canonical(argv[8], target.executable) && target.executable == argv[8];
}
static CFTypeRef attribute(AXUIElementRef element, CFStringRef key, AXError *status = nullptr) {
  CFTypeRef value = nullptr;
  const AXError error = AXUIElementCopyAttributeValue(element, key, &value);
  if (status) *status = error;
  if (error != kAXErrorSuccess) return nullptr;
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
static AXUIElementRef ownWindow(AXUIElementRef app, const Target &target) {
  CFTypeRef value = attribute(app, kAXWindowsAttribute);
  if (!value) return nullptr;
  AXUIElementRef result = nullptr;
  if (CFGetTypeID(value) == CFArrayGetTypeID() && CFArrayGetCount(static_cast<CFArrayRef>(value)) <= 8) {
    NSString *title = [@"Ny GTK directed " stringByAppendingString:[NSString stringWithUTF8String:target.nonce.c_str()]];
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
static bool focusedField(AXUIElementRef app, AXUIElementRef window, const Target &target,
                         NSMutableDictionary *observed) {
  AXError error = kAXErrorSuccess;
  // Retain the previous observation, but keyboard-element focus is not the
  // public contract for an application's focused window.
  CFTypeRef legacy = attribute(window, kAXFocusedAttribute, &error);
  observed[@"legacy_window_error"] = @(error);
  observed[@"legacy_window_boolean"] = @(legacy && CFGetTypeID(legacy) == CFBooleanGetTypeID());
  observed[@"legacy_window_value"] = @(legacy && CFEqual(legacy, kCFBooleanTrue));
  if (legacy) CFRelease(legacy);
  CFTypeRef focusedWindow = attribute(app, kAXFocusedWindowAttribute, &error);
  observed[@"app_window_error"] = @(error);
  const bool appElement = focusedWindow && CFGetTypeID(focusedWindow) == AXUIElementGetTypeID();
  const bool appOwner = appElement && owner(static_cast<AXUIElementRef>(const_cast<void *>(focusedWindow)), target.pid);
  const bool appMatch = appOwner && CFEqual(focusedWindow, window);
  observed[@"app_window_element"] = @(appElement);
  observed[@"app_window_pid_matches"] = @(appOwner);
  observed[@"app_window_matches"] = @(appMatch);
  if (focusedWindow) CFRelease(focusedWindow);
  CFTypeRef field = attribute(app, kAXFocusedUIElementAttribute, &error);
  observed[@"field_error"] = @(error);
  const bool fieldElement = field && CFGetTypeID(field) == AXUIElementGetTypeID();
  auto element = fieldElement ? static_cast<AXUIElementRef>(const_cast<void *>(field)) : nullptr;
  const bool fieldOwner = element && owner(element, target.pid);
  observed[@"field_element"] = @(fieldElement);
  observed[@"field_pid_matches"] = @(fieldOwner);
  bool roleMatch = false, windowMatch = false;
  observed[@"field_role_error"] = @(-1); observed[@"field_window_error"] = @(-1);
  observed[@"field_role_category"] = @"unavailable";
  observed[@"field_text_role"] = @NO; observed[@"field_window_element"] = @NO;
  observed[@"field_window_pid_matches"] = @NO; observed[@"field_window_matches"] = @NO;
  if (fieldOwner && AXUIElementSetMessagingTimeout(element, 1.0f) == kAXErrorSuccess) {
    CFTypeRef role = attribute(element, kAXRoleAttribute, &error);
    roleMatch = role && CFGetTypeID(role) == CFStringGetTypeID() && CFEqual(role, kAXTextFieldRole);
    observed[@"field_role_error"] = @(error); observed[@"field_text_role"] = @(roleMatch);
    if (role && CFGetTypeID(role) == CFStringGetTypeID())
      observed[@"field_role_category"] = roleMatch ? @"text_field" : CFEqual(role, kAXTextAreaRole) ? @"text_area" : @"other";
    if (role) CFRelease(role);
    CFTypeRef fieldWindow = attribute(element, kAXWindowAttribute, &error);
    const bool isElement = fieldWindow && CFGetTypeID(fieldWindow) == AXUIElementGetTypeID();
    const bool samePid = isElement && owner(static_cast<AXUIElementRef>(const_cast<void *>(fieldWindow)), target.pid);
    windowMatch = samePid && CFEqual(fieldWindow, window);
    observed[@"field_window_error"] = @(error); observed[@"field_window_element"] = @(isElement);
    observed[@"field_window_pid_matches"] = @(samePid); observed[@"field_window_matches"] = @(windowMatch);
    if (fieldWindow) CFRelease(fieldWindow);
  }
  if (field) CFRelease(field);
  // AccessKit is disabled in this provider profile. Widget AX exposure is
  // diagnostic only; the child proves actual GTK entry focus before readiness.
  (void)windowMatch;
  return appMatch;
}
static int driver(const char *mode, const Target &target) {
  bool identity = target.verify();
  const bool post = CGPreflightPostEventAccess(), ax = AXIsProcessTrusted();
  bool selected = false, focused = false, exactTimestamps = false, pressed = false;
  unsigned posted = 0;
  NSMutableDictionary *focusObservation = [NSMutableDictionary dictionary];
  AXUIElementRef app = nullptr, window = nullptr; CFTypeRef button = nullptr;
  if (identity && post && ax) {
    app = AXUIElementCreateApplication(target.pid);
    if (app && AXUIElementSetMessagingTimeout(app, 1.0f) == kAXErrorSuccess) window = ownWindow(app, target);
    selected = window != nullptr;
    if (selected) {
      focused = AXUIElementSetMessagingTimeout(window, 1.0f) == kAXErrorSuccess &&
        focusedField(app, window, target, focusObservation);
      if (!std::strcmp(mode, "keys") && focused) {
        exactTimestamps = focused;
        for (unsigned i = 0; focused && i < 10; ++i) {
          for (unsigned down = 0; down < 2; ++down) {
            if (!target.verify()) { identity = false; break; }
            CGEventRef event = CGEventCreateKeyboardEvent(nullptr, keyCodes[i], down == 0);
            if (!event) { exactTimestamps = false; break; }
            UniChar text = static_cast<UniChar>(expectedText[i]);
            CGEventKeyboardSetUnicodeString(event, 1, &text);
            CGEventSetFlags(event, i == 0 ? kCGEventFlagMaskShift : 0);
            const CGEventTimestamp timestamp = (target.baseSeconds + i * 2 + down) * uint64_t{1000000000};
            CGEventSetTimestamp(event, timestamp);
            const bool exact = CGEventGetTimestamp(event) == timestamp;
            exactTimestamps = exactTimestamps && exact;
            if (exact) { CGEventPostToPid(target.pid, event); ++posted; }
            CFRelease(event);
            if (!exact) break;
          }
          if (!identity || !exactTimestamps) break;
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
    (!std::strcmp(mode, "keys") ? exactTimestamps && posted == 20 : pressed);
  emit(@{@"schema": @1, @"phase": [NSString stringWithUTF8String:mode], @"qualification": @NO,
      @"identity_verified": @(identity), @"post_event_access": @(post), @"ax_trusted": @(ax),
      @"own_window_selected": @(selected), @"focus_verified": @(focused), @"timestamp_readback": @(exactTimestamps),
      @"focus_observation": focusObservation,
      @"base_seconds": @(target.baseSeconds), @"events_posted": @(posted), @"close_action": @(pressed), @"success": @(success)});
  return success ? 0 : 2;
}


using Clock = std::chrono::steady_clock;
struct GtkState {
  GtkWindow *window = nullptr;
  GtkWidget *entry = nullptr, *checkbox = nullptr, *button = nullptr;
  GdkSurface *surface = nullptr;
  uint64_t baseSeconds = 0;
  unsigned eventCount = 0, changed = 0, toggled = 0, clicked = 0, closed = 0;
  unsigned changedAt[6]{}, toggledAt = 0, clickedAt = 0;
  bool invalid = false;
  struct Event { guint keyval, keycode, modifiers; guint32 timestamp; int type; bool release, ownSurface, matches; } events[20]{};
};
static void keyObservation(GtkEventControllerKey *controller, guint keyval, guint keycode,
                           GdkModifierType modifiers, bool release, gpointer data) {
  auto &state = *static_cast<GtkState *>(data);
  GdkEvent *event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(controller));
  if (!event || state.eventCount >= 20) { state.invalid = true; return; }
  const unsigned index = state.eventCount, key = index / 2;
  const guint expectedModifiers = key == 0 ? GDK_SHIFT_MASK : 0;
  const guint32 expectedTime = static_cast<guint32>((state.baseSeconds + index) * 1000);
  const bool own = gdk_event_get_surface(event) == state.surface;
  const bool match = own && keyval == keyValues[key] && keycode == keyCodes[key] &&
      modifiers == expectedModifiers && gdk_event_get_modifier_state(event) == expectedModifiers &&
      gdk_event_get_time(event) == expectedTime && release == bool(index % 2) &&
      gdk_event_get_event_type(event) == (release ? GDK_KEY_RELEASE : GDK_KEY_PRESS);
  state.events[state.eventCount++] = {keyval, keycode, static_cast<guint>(modifiers), gdk_event_get_time(event), static_cast<int>(gdk_event_get_event_type(event)), release, own, match};
  if (!match) state.invalid = true;
}
static gboolean pressed(GtkEventControllerKey *controller, guint keyval, guint keycode,
                         GdkModifierType modifiers, gpointer data) {
  keyObservation(controller, keyval, keycode, modifiers, false, data); return FALSE;
}
static void released(GtkEventControllerKey *controller, guint keyval, guint keycode,
                      GdkModifierType modifiers, gpointer data) {
  keyObservation(controller, keyval, keycode, modifiers, true, data);
}
static void changed(GtkEditable *entry, gpointer data) {
  auto &state = *static_cast<GtkState *>(data); ++state.changed;
  if (state.changed > 6) { state.invalid = true; return; }
  state.changedAt[state.changed - 1] = state.eventCount;
  if (state.eventCount != state.changed * 2 - 1 || !state.events[state.eventCount - 1].matches ||
      std::string(gtk_editable_get_text(entry)) != std::string("Matrix", state.changed))
    state.invalid = true; // Never log unknown input content.
}
static void toggled(GtkCheckButton *button, gpointer data) {
  auto &state = *static_cast<GtkState *>(data); ++state.toggled;
  state.toggledAt = state.eventCount;
  if (state.toggled != 1 || (state.eventCount != 15 && state.eventCount != 16) ||
      !state.events[state.eventCount - 1].matches || !gtk_check_button_get_active(button)) state.invalid = true;
}
static void clicked(GtkButton *, gpointer data) {
  auto &state = *static_cast<GtkState *>(data); ++state.clicked; state.clickedAt = state.eventCount;
  if (state.clicked != 1 || (state.eventCount != 19 && state.eventCount != 20) ||
      !state.events[state.eventCount - 1].matches) state.invalid = true;
}
static gboolean closeRequest(GtkWindow *, gpointer data) {
  ++static_cast<GtkState *>(data)->closed;
  return FALSE; // Normal GTK close handling must actually destroy the native window.
}
static bool entryFocused(const GtkState &state) {
  GtkWidget *focus = gtk_window_get_focus(state.window);
  return focus && (focus == state.entry || gtk_widget_is_ancestor(focus, state.entry));
}
static int gtkTarget(const char *nonce) {
  const auto start = Clock::now(), limit = start + std::chrono::seconds(25);
  GtkState state;
  bool ready = false, typed = false, expired = false, exception = false, initialized = false;
  __strong NSWindow *native = nil;
  @try {
    if (!pthread_main_np() || g_strcmp0(g_getenv("GDK_BACKEND"), "macos") ||
        g_strcmp0(g_getenv("GSK_RENDERER"), "cairo") || !gtk_init_check())
      @throw [NSException exceptionWithName:@"GTKInitialization" reason:@"private GTK main-thread profile unavailable" userInfo:nil];
    initialized = true;
    if (g_list_model_get_n_items(gtk_window_get_toplevels()) != 0)
      @throw [NSException exceptionWithName:@"GTKWindow" reason:@"unexpected existing windows" userInfo:nil];
    const double uptime = std::floor(NSProcessInfo.processInfo.systemUptime);
    if (!std::isfinite(uptime) || uptime < 33 || uptime > 4294967296.0)
      @throw [NSException exceptionWithName:@"Timestamp" reason:@"bounded uptime required" userInfo:nil];
    state.baseSeconds = static_cast<uint64_t>(uptime) - 32;
    state.window = GTK_WINDOW(gtk_window_new()); g_object_ref(state.window);
    const std::string title = std::string("Ny GTK directed ") + nonce;
    gtk_window_set_title(state.window, title.c_str()); gtk_window_set_default_size(state.window, 360, 180);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    state.entry = gtk_entry_new(); state.checkbox = gtk_check_button_new_with_label("Enable setting");
    state.button = gtk_button_new_with_label("Finish probe");
    gtk_box_append(GTK_BOX(box), state.entry); gtk_box_append(GTK_BOX(box), state.checkbox);
    gtk_box_append(GTK_BOX(box), state.button); gtk_window_set_child(state.window, box);
    g_signal_connect(state.entry, "changed", G_CALLBACK(changed), &state);
    g_signal_connect(state.checkbox, "toggled", G_CALLBACK(toggled), &state);
    g_signal_connect(state.button, "clicked", G_CALLBACK(clicked), &state);
    g_signal_connect(state.window, "close-request", G_CALLBACK(closeRequest), &state);
    GtkEventController *controller = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(controller, GTK_PHASE_CAPTURE);
    g_signal_connect(controller, "key-pressed", G_CALLBACK(pressed), &state);
    g_signal_connect(controller, "key-released", G_CALLBACK(released), &state);
    gtk_widget_add_controller(GTK_WIDGET(state.window), controller);
    gtk_window_present(state.window);
    if (!gtk_widget_grab_focus(state.entry))
      @throw [NSException exceptionWithName:@"GTKFocus" reason:@"entry focus request failed" userInfo:nil];
    while (Clock::now() < limit) {
      g_main_context_iteration(nullptr, FALSE);
      if (!ready && gtk_widget_get_mapped(GTK_WIDGET(state.window))) {
        state.surface = gtk_native_get_surface(GTK_NATIVE(state.window));
        if (!state.surface || !GDK_IS_MACOS_SURFACE(state.surface))
          @throw [NSException exceptionWithName:@"GTKSurface" reason:@"Macos surface required" userInfo:nil];
        native = (__bridge NSWindow *)gdk_macos_surface_get_native_window(GDK_MACOS_SURFACE(state.surface));
        if (native && native.windowNumber > 0 && [NSApp.windows containsObject:native] && native.visible &&
            native.keyWindow && NSApp.active && entryFocused(state)) {
          ready = true;
          emit(@{@"schema": @1, @"phase": @"ready", @"qualification": @NO, @"pid": @(getpid()),
            @"window_number": @(native.windowNumber), @"main_thread": @YES, @"gtk_entry_focused": @YES,
            @"active": @YES, @"key_window": @YES, @"base_seconds": @(state.baseSeconds),
            @"gtk_header": [NSString stringWithFormat:@"%u.%u.%u", GTK_MAJOR_VERSION, GTK_MINOR_VERSION, GTK_MICRO_VERSION],
            @"gtk_runtime": [NSString stringWithFormat:@"%u.%u.%u", gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version()],
            @"origins": @{@"gtk": loadedOrigin(reinterpret_cast<const void *>(&gtk_init_check)),
              @"gobject": loadedOrigin(reinterpret_cast<const void *>(&g_object_ref)),
              @"glib": loadedOrigin(reinterpret_cast<const void *>(&g_main_context_iteration)),
              @"gio": loadedOrigin(reinterpret_cast<const void *>(&g_list_model_get_n_items))}});
        }
      }
      if (!typed && ready && state.eventCount == 20 && state.changed == 6 && state.clicked == 1 && state.toggled == 1 &&
          !state.invalid && !std::strcmp(gtk_editable_get_text(GTK_EDITABLE(state.entry)), "Matrix") &&
          gtk_check_button_get_active(GTK_CHECK_BUTTON(state.checkbox))) {
        typed = true;
        emit(@{@"schema": @1, @"phase": @"typed", @"qualification": @NO, @"text": @"Matrix",
          @"changed": @6, @"toggled": @1, @"clicked": @1, @"events": @20, @"checked": @YES});
      }
      if (state.invalid || state.closed) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expired = Clock::now() >= limit;
  } @catch (NSException *) { exception = true; }
  const bool genuineClose = initialized && typed && state.closed == 1 &&
      g_list_model_get_n_items(gtk_window_get_toplevels()) == 0 && native && !native.visible;
  bool cleaned = false;
  @try {
    if (state.window) gtk_window_destroy(state.window);
    while (initialized && Clock::now() < start + std::chrono::seconds(29) &&
           (g_list_model_get_n_items(gtk_window_get_toplevels()) != 0 || (native && native.visible))) {
      g_main_context_iteration(nullptr, FALSE);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    cleaned = !initialized || (g_list_model_get_n_items(gtk_window_get_toplevels()) == 0 && (!native || !native.visible));
    if (state.window) { g_object_unref(state.window); state.window = nullptr; }
  } @catch (NSException *) { cleaned = false; }
  NSMutableArray *events = [NSMutableArray array];
  for (unsigned i = 0; i < state.eventCount; ++i) {
    const auto &value = state.events[i];
    [events addObject:@{@"index": @(i), @"keyval": @(value.keyval), @"keycode": @(value.keycode),
      @"modifiers": @(value.modifiers), @"time_ms": @(value.timestamp), @"event_type": @(value.type), @"release": @(value.release),
      @"own_surface": @(value.ownSurface), @"matches": @(value.matches)}];
  }
  NSMutableArray *changedAt = [NSMutableArray array];
  for (unsigned value : state.changedAt) [changedAt addObject:@(value)];
  const bool success = ready && typed && genuineClose && cleaned && !expired && !exception && !state.invalid;
  emit(@{@"schema": @1, @"phase": @"final", @"qualification": @NO, @"ready": @(ready), @"typed": @(typed),
    @"gdk_events": events, @"changed": @(state.changed), @"clicked": @(state.clicked), @"toggled": @(state.toggled),
    @"changed_at": changedAt, @"toggled_at": @(state.toggledAt), @"clicked_at": @(state.clickedAt),
    @"close_requests": @(state.closed), @"native_close": @(genuineClose), @"cleanup_verified": @(cleaned),
    @"expired": @(expired), @"exception": @(exception), @"invalid": @(state.invalid), @"success": @(success)});
  return success ? 0 : 2;
}
int main(int argc, const char **argv) {
  @autoreleasepool {
    if (argc == 3 && !std::strcmp(argv[1], "target") && nonceValid(argv[2])) return gtkTarget(argv[2]);
    if (argc > 1 && (!std::strcmp(argv[1], "keys") || !std::strcmp(argv[1], "close"))) {
      Target value; if (parseTarget(argc, argv, value)) return driver(argv[1], value);
    }
    std::fputs("invalid private GTK directed-probe arguments\n", stderr); return 2;
  }
}
