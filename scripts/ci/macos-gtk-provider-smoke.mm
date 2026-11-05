// Private provider feasibility only. All events target this process's GTK window.
// GTK owns AppKit integration; this probe only iterates the default GLib context.
#import <AppKit/AppKit.h>
#include <gtk/gtk.h>
#include <gdk/macos/gdkmacos.h>
#include <pango/pangocairo.h>
#include <pango/pangocoretext.h>
#include <png.h>
#include <dlfcn.h>
#include <pthread.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <thread>

using Clock = std::chrono::steady_clock;
static void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
static NSString *text(const char *value) {
  return value ? [NSString stringWithUTF8String:value] : @"";
}
static NSDictionary *origin(const void *symbol, bool fileRequired = true) {
  Dl_info info{};
  require(dladdr(symbol, &info) != 0 && info.dli_fname, "loaded symbol origin unavailable");
  char *canonical = realpath(info.dli_fname, nullptr);
  require(canonical != nullptr || !fileRequired, "loaded symbol origin is not file-backed");
  NSString *path = canonical ? text(canonical) : nil;
  free(canonical);
  require((!path || (path.length > 0 && path.length <= 4096)) && std::strlen(info.dli_fname) <= 4096,
          "invalid origin path");
  // Apple framework install names can be backed by the dyld shared cache rather
  // than a readable individual file. Report that distinction; do not invent a hash.
  return @{@"declared":text(info.dli_fname), @"canonical":path ?: (id)[NSNull null],
      @"file_backed":@(path != nil)};
}

struct State {
  struct Key {
    unsigned stage, keyval, keycode, modifiers;
    guint32 time;
    bool release, own_surface, matches;
  };
  GtkWindow *window = nullptr;
  GtkWidget *entry = nullptr, *button = nullptr, *check = nullptr;
  GdkSurface *surface = nullptr; // Borrowed only while the window is realized.
  unsigned stage = 0, presses[3]{}, releases[3]{};
  guint32 times[3]{};
  unsigned changed = 0, clicked = 0, toggled = 0, closed = 0;
  unsigned unexpected_keys = 0;
  Key keys[6]{};
  unsigned key_count = 0;
  bool invalid = false;
};
static void observeKey(GtkEventControllerKey *controller, guint keyval, guint keycode,
                       GdkModifierType modifiers, bool release, State *state) {
  GdkEvent *event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(controller));
  if (state->stage >= 3 || !event || gdk_event_get_surface(event) != state->surface) {
    state->invalid = true;
    return;
  }
  const unsigned stage = state->stage;
  const bool match = keyval == (stage == 0 ? GDK_KEY_n : GDK_KEY_space) &&
      keycode == (stage == 0 ? 45U : 49U) && modifiers == 0 &&
      gdk_event_get_modifier_state(event) == 0 &&
      gdk_event_get_time(event) == state->times[stage] &&
      gdk_event_get_event_type(event) == (release ? GDK_KEY_RELEASE : GDK_KEY_PRESS);
  if (state->key_count < 6) state->keys[state->key_count++] = {
      stage,keyval,keycode,static_cast<unsigned>(modifiers),gdk_event_get_time(event),release,
      gdk_event_get_surface(event) == state->surface,match};
  else state->invalid = true;
  if (!match) {
    ++state->unexpected_keys; // No unrelated character/title data is retained.
    state->invalid = true;
    return;
  }
  auto &count = release ? state->releases[stage] : state->presses[stage];
  if (++count != 1) state->invalid = true;
}
static gboolean keyPressed(GtkEventControllerKey *controller, guint keyval, guint keycode,
                           GdkModifierType modifiers, gpointer data) {
  observeKey(controller, keyval, keycode, modifiers, false, static_cast<State *>(data));
  return FALSE; // Observe; leave normal GTK widget handling intact.
}
static void keyReleased(GtkEventControllerKey *controller, guint keyval, guint keycode,
                        GdkModifierType modifiers, gpointer data) {
  observeKey(controller, keyval, keycode, modifiers, true, static_cast<State *>(data));
}
static void changed(GtkEditable *, gpointer data) { ++static_cast<State *>(data)->changed; }
static void clicked(GtkButton *, gpointer data) { ++static_cast<State *>(data)->clicked; }
static void toggled(GtkCheckButton *, gpointer data) { ++static_cast<State *>(data)->toggled; }
static gboolean closed(GtkWindow *, gpointer data) {
  ++static_cast<State *>(data)->closed;
  return FALSE;
}

template <typename Predicate>
static bool pumpUntil(Clock::time_point limit, Predicate done) {
  // No blocking iteration or unbounded drain: even a busy source observes time.
  while (Clock::now() < limit) {
    if (done()) return true;
    g_main_context_iteration(nullptr, FALSE);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return done();
}
static bool focused(State &state, GtkWidget *target) {
  GtkWidget *focus = gtk_window_get_focus(state.window);
  return focus && (focus == target || gtk_widget_is_ancestor(focus, target));
}
static NSDictionary *queueKeys(State &state, NSWindow *native, double seconds) {
  require(std::isfinite(seconds) && seconds >= 1 && seconds <= 4294967296.0 &&
          std::floor(seconds) == seconds, "timestamp must be a bounded whole second");
  NSString *characters = state.stage == 0 ? @"n" : @" ";
  const unsigned short code = state.stage == 0 ? 45 : 49;
  state.times[state.stage] = static_cast<guint32>(static_cast<guint64>(seconds * 1000.0));
  for (const NSEventType type : {NSEventTypeKeyDown, NSEventTypeKeyUp}) {
    NSEvent *event = [NSEvent keyEventWithType:type location:NSZeroPoint modifierFlags:0
        timestamp:seconds windowNumber:native.windowNumber context:nil characters:characters
        charactersIgnoringModifiers:characters isARepeat:NO keyCode:code];
    require(event && event.window == native && event.windowNumber == native.windowNumber &&
            event.timestamp == seconds && event.type == type && event.keyCode == code &&
            event.modifierFlags == 0 && [event.characters isEqualToString:characters] &&
            [event.charactersIgnoringModifiers isEqualToString:characters], "queued event metadata mismatch");
    [NSApp postEvent:event atStart:NO];
  }
  char encoded[64];
  std::snprintf(encoded, sizeof encoded, "%a", seconds);
  return @{@"stage":@(state.stage), @"timestamp_seconds":@(seconds),
      @"timestamp_hex":text(encoded), @"gdk_time_ms":@(state.times[state.stage]),
      @"keycode":@(code), @"keyval":@(state.stage == 0 ? GDK_KEY_n : GDK_KEY_space),
      @"modifiers":@0, @"queued_count":@2, @"window_number":@(native.windowNumber)};
}

static NSDictionary *fontProof(GtkWidget *widget) {
  PangoContext *context = gtk_widget_get_pango_context(widget);
  require(context != nullptr, "GTK Pango context absent");
  PangoFontMap *map = pango_context_get_font_map(context);
  require(PANGO_IS_CAIRO_FONT_MAP(map), "Pango font map is not Cairo");
  require(pango_cairo_font_map_get_font_type(PANGO_CAIRO_FONT_MAP(map)) == CAIRO_FONT_TYPE_QUARTZ,
          "Pango Cairo font map is not Quartz");
  PangoFontDescription *description = pango_font_description_from_string("Sans 12");
  require(description != nullptr, "font description failed");
  PangoFont *font = pango_context_load_font(context, description);
  pango_font_description_free(description);
  require(font != nullptr, "CoreText font load failed");
  const bool coretext = PANGO_IS_CORE_TEXT_FONT(font);
  CTFontRef native = coretext ? pango_core_text_font_get_ctfont(PANGO_CORE_TEXT_FONT(font)) : nullptr;
  const bool nativeValid = native && CTFontGetSize(native) > 0;
  NSString *fontType = text(G_OBJECT_TYPE_NAME(font));
  g_object_unref(font);
  require(coretext && nativeValid, "actual font is not backed by CoreText");
  PangoLayout *layout = pango_layout_new(context);
  require(layout != nullptr, "Pango layout failed");
  pango_layout_set_text(layout, "Ny GTK", -1);
  int width = 0, height = 0;
  pango_layout_get_pixel_size(layout, &width, &height);
  g_object_unref(layout);
  require(width > 0 && width < 4096 && height > 0 && height < 4096, "invalid Pango layout size");
  return @{@"font_type":fontType, @"font_map_type":text(G_OBJECT_TYPE_NAME(map)),
      @"quartz":@YES, @"coretext":@YES, @"width":@(width), @"height":@(height)};
}
static NSDictionary *pngProof() {
  static const guchar bytes[] = {
    0x89,0x50,0x4e,0x47,0xd,0xa,0x1a,0xa,0,0,0,0xd,0x49,0x48,0x44,0x52,
    0,0,0,1,0,0,0,1,8,6,0,0,0,0x1f,0x15,0xc4,0x89,0,0,0,0xd,0x49,0x44,0x41,
    0x54,0x78,0x9c,0x63,0x10,0x54,0x32,0xfe,0xf,0,2,0x14,1,0x66,4,0xe0,0xde,
    0x7f,0,0,0,0,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
  GError *error = nullptr;
  GdkPixbufLoader *loader = gdk_pixbuf_loader_new_with_type("png", &error);
  if (error) g_error_free(error);
  require(loader != nullptr, "PNG loader missing");
  error = nullptr;
  bool okay = gdk_pixbuf_loader_write(loader, bytes, sizeof bytes, &error);
  if (error) g_error_free(error);
  error = nullptr;
  const bool complete = gdk_pixbuf_loader_close(loader, &error);
  if (error) g_error_free(error);
  GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
  okay = okay && complete && pixbuf && gdk_pixbuf_get_width(pixbuf) == 1 &&
      gdk_pixbuf_get_height(pixbuf) == 1 && gdk_pixbuf_get_n_channels(pixbuf) == 4 &&
      gdk_pixbuf_get_has_alpha(pixbuf) && gdk_pixbuf_get_bits_per_sample(pixbuf) == 8;
  const guchar *pixel = okay ? gdk_pixbuf_read_pixels(pixbuf) : nullptr;
  okay = okay && pixel && pixel[0] == 17 && pixel[1] == 34 && pixel[2] == 51 && pixel[3] == 255;
  g_object_unref(loader);
  require(okay, "PNG pixel oracle failed");
  return @{@"width":@1, @"height":@1, @"channels":@4, @"rgba":@[@17,@34,@51,@255], @"decoded":@YES};
}

int main(int argc, char **) {
  const auto started = Clock::now();
  const auto workLimit = started + std::chrono::seconds(24);
  const auto wholeLimit = started + std::chrono::seconds(29);
  @autoreleasepool {
    NSMutableDictionary *report = [@{@"schema":@"nil.macos-gtk-provider-smoke.v1",
        @"scope":@"private-provider-own-process", @"status":@"FAIL",
        @"language_acceptance":@NO, @"main_thread":@(pthread_main_np() != 0)} mutableCopy];
    State state;
    __strong NSWindow *native = nil;
    bool initialized = false, bodyPassed = false;
    @try {
      try {
        require(argc == 1, "smoke takes no arguments");
        require(pthread_main_np() != 0, "GTK must initialize on process main thread");
        require(g_strcmp0(g_getenv("GDK_BACKEND"), "macos") == 0 &&
                g_strcmp0(g_getenv("GSK_RENDERER"), "cairo") == 0, "explicit Macos/Cairo profile required");
        require(gtk_init_check(), "gtk_init_check failed");
        initialized = true;
        GdkDisplay *display = gdk_display_get_default();
        require(display && GDK_IS_MACOS_DISPLAY(display), "live display is not Macos");
        require(g_list_model_get_n_items(gtk_window_get_toplevels()) == 0, "unexpected initial GTK windows");
        report[@"backend"] = text(G_OBJECT_TYPE_NAME(display));
        report[@"versions"] = @{@"gtk_header":[NSString stringWithFormat:@"%u.%u.%u",GTK_MAJOR_VERSION,GTK_MINOR_VERSION,GTK_MICRO_VERSION],
            @"gtk_runtime":[NSString stringWithFormat:@"%u.%u.%u",gtk_get_major_version(),gtk_get_minor_version(),gtk_get_micro_version()],
            @"pango_header":text(PANGO_VERSION_STRING), @"pango_runtime":text(pango_version_string()),
            @"pixbuf_header":text(GDK_PIXBUF_VERSION), @"pixbuf_runtime":text(gdk_pixbuf_version),
            @"png_header":text(PNG_LIBPNG_VER_STRING), @"png_runtime":text(png_get_libpng_ver(nullptr))};
        require(gtk_get_major_version() == GTK_MAJOR_VERSION && gtk_get_minor_version() == GTK_MINOR_VERSION &&
                gtk_get_micro_version() == GTK_MICRO_VERSION &&
                std::strcmp(PANGO_VERSION_STRING,pango_version_string()) == 0 &&
                std::strcmp(GDK_PIXBUF_VERSION,gdk_pixbuf_version) == 0 &&
                std::strcmp(PNG_LIBPNG_VER_STRING,png_get_libpng_ver(nullptr)) == 0, "header/runtime version mismatch");
        report[@"origins"] = @{@"gtk":origin((const void *)&gtk_init_check),
            @"pango":origin((const void *)&pango_core_text_font_get_type),
            @"pangocairo":origin((const void *)&pango_cairo_font_map_get_default),
            @"pixbuf":origin((const void *)&gdk_pixbuf_loader_new_with_type),
            @"glib":origin((const void *)&g_main_context_iteration),
            @"gobject":origin((const void *)&g_object_ref), @"cairo":origin((const void *)&cairo_version_string),
            @"png":origin((const void *)&png_get_libpng_ver),
            @"coretext":origin((const void *)&CTFontGetSize, false), @"appkit":origin((const void *)&NSRectFill, false)};
        report[@"png"] = pngProof();
        state.window = GTK_WINDOW(gtk_window_new());
        g_object_ref(state.window); // Keep one own reference through default close handling.
        gtk_window_set_title(state.window, "Ny private GTK provider smoke");
        gtk_window_set_default_size(state.window, 360, 180);
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        state.entry = gtk_entry_new();
        state.button = gtk_button_new_with_label("Probe button");
        state.check = gtk_check_button_new_with_label("Probe checkbox");
        gtk_box_append(GTK_BOX(box), state.entry);
        gtk_box_append(GTK_BOX(box), state.button);
        gtk_box_append(GTK_BOX(box), state.check);
        gtk_window_set_child(state.window, box);
        g_signal_connect(state.entry, "changed", G_CALLBACK(changed), &state);
        g_signal_connect(state.button, "clicked", G_CALLBACK(clicked), &state);
        g_signal_connect(state.check, "toggled", G_CALLBACK(toggled), &state);
        g_signal_connect(state.window, "close-request", G_CALLBACK(closed), &state);
        GtkEventController *controller = gtk_event_controller_key_new();
        gtk_event_controller_set_propagation_phase(controller, GTK_PHASE_CAPTURE);
        g_signal_connect(controller, "key-pressed", G_CALLBACK(keyPressed), &state);
        g_signal_connect(controller, "key-released", G_CALLBACK(keyReleased), &state);
        gtk_widget_add_controller(GTK_WIDGET(state.window), controller);
        gtk_window_present(state.window);
        require(pumpUntil(workLimit, [&] {return gtk_widget_get_mapped(GTK_WIDGET(state.window));}), "GTK map timeout");
        state.surface = gtk_native_get_surface(GTK_NATIVE(state.window));
        require(state.surface && GDK_IS_MACOS_SURFACE(state.surface), "window surface is not Macos");
        native = (__bridge NSWindow *)gdk_macos_surface_get_native_window(GDK_MACOS_SURFACE(state.surface));
        require(native && native.windowNumber > 0 && [NSApp.windows containsObject:native],
                "public native window unavailable/not in own application");
        GskRenderer *renderer = gtk_native_get_renderer(GTK_NATIVE(state.window));
        require(renderer && GSK_IS_CAIRO_RENDERER(renderer), "actual renderer is not Cairo");
        report[@"renderer"] = text(G_OBJECT_TYPE_NAME(renderer));
        require(pumpUntil(workLimit, [&] {return native.visible && native.keyWindow && NSApp.active;}), "owned window not active/key/visible");
        report[@"font"] = fontProof(state.entry);
        require(state.changed == 0 && state.clicked == 0 && state.toggled == 0, "widget callbacks before input");
        NSMutableArray *events = [NSMutableArray array];
        report[@"events"] = events;
        const double baseTime = std::floor(NSProcessInfo.processInfo.systemUptime) - 3;
        GtkWidget *targets[] = {state.entry, state.button, state.check};
        for (state.stage = 0; state.stage < 3; ++state.stage) {
          require(gtk_widget_grab_focus(targets[state.stage]), "widget focus request failed");
          require(pumpUntil(workLimit, [&] {return focused(state,targets[state.stage]);}), "widget focus timeout");
          [events addObject:queueKeys(state,native,baseTime + state.stage)];
          require(pumpUntil(workLimit, [&] {
            const bool outcome = state.stage == 0 ? state.changed == 1 : state.stage == 1 ? state.clicked == 1 : state.toggled == 1;
            return state.invalid || (outcome && state.presses[state.stage] == 1 && state.releases[state.stage] == 1);
          }), "queued GTK input/outcome timeout");
          require(!state.invalid, "unexpected/duplicate GTK key observation");
        }
        require(std::strcmp(gtk_editable_get_text(GTK_EDITABLE(state.entry)), "n") == 0 &&
                state.changed == 1 && state.clicked == 1 && state.toggled == 1 &&
                gtk_check_button_get_active(GTK_CHECK_BUTTON(state.check)), "widget outcome mismatch");
        report[@"widgets"] = @{@"entry_text":@"n", @"entry_changed":@(state.changed),
            @"button_clicked":@(state.clicked), @"checkbox_toggled":@(state.toggled), @"checkbox_active":@YES};
        [native performClose:nil]; // Only this owned window; normal GTK close-request path.
        require(pumpUntil(workLimit, [&] {return state.closed == 1 && g_list_model_get_n_items(gtk_window_get_toplevels()) == 0 && !native.visible;}), "normal close timeout");
        bodyPassed = true;
      } catch (const std::exception &error) { report[@"error"] = text(error.what()); }
    } @catch (NSException *error) {
      report[@"error"] = @"Objective-C exception";
      report[@"exception_name"] = error.name ?: @"unknown";
    }
    // Failure cleanup is independent of acceptance; never change FAIL into PASS.
    bool cleanup = false, ownWindowHidden = false;
    @try {
      if (state.window) gtk_window_destroy(state.window);
      cleanup = !initialized || pumpUntil(wholeLimit, [&] {
        return g_list_model_get_n_items(gtk_window_get_toplevels()) == 0 && (!native || !native.visible);
      });
      ownWindowHidden = !native || !native.visible;
      if (state.window) { g_object_unref(state.window); state.window = nullptr; }
      native = nil;
    } @catch (NSException *) { cleanup = false; }
    report[@"observed_keys"] = @[@{@"pressed":@(state.presses[0]), @"released":@(state.releases[0])},
        @{@"pressed":@(state.presses[1]), @"released":@(state.releases[1])},
        @{@"pressed":@(state.presses[2]), @"released":@(state.releases[2])}];
    report[@"unexpected_keys"] = @(state.unexpected_keys);
    NSMutableArray *dispatched = [NSMutableArray array];
    for (unsigned index = 0; index < state.key_count; ++index) {
      const auto &key = state.keys[index];
      [dispatched addObject:@{@"stage":@(key.stage), @"keyval":@(key.keyval), @"keycode":@(key.keycode),
          @"modifiers":@(key.modifiers), @"gdk_time_ms":@(key.time), @"release":@(key.release),
          @"own_surface":@(key.own_surface), @"matches":@(key.matches)}];
    }
    report[@"dispatched_keys"] = dispatched;
    report[@"close_requests"] = @(state.closed);
    report[@"cleanup"] = @{@"verified":@(cleanup), @"own_window_hidden":@(ownWindowHidden),
        @"gtk_toplevel_count":@(initialized ? g_list_model_get_n_items(gtk_window_get_toplevels()) : 0)};
    const double elapsed = std::chrono::duration<double>(Clock::now()-started).count();
    report[@"elapsed_seconds"] = @(elapsed);
    const bool passed = bodyPassed && cleanup && ownWindowHidden && !state.invalid && state.key_count == 6 && elapsed <= 29;
    report[@"status"] = passed ? @"PASS" : @"FAIL";
    NSError *error = nil;
    NSData *json = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingSortedKeys error:&error];
    if (!json || json.length > 32768 || std::fwrite(json.bytes, 1, json.length, stdout) != json.length || std::putchar('\n') == EOF)
      return 2;
    return passed ? 0 : 1;
  }
}
