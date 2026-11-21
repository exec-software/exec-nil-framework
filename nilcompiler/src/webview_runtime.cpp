/* Copyright (C) 2026 Execute Software. SPDX-License-Identifier: MIT */
#include "webview_runtime.hpp"
#include "../../common/webview_provider_v1.h"
#include "../../common/webview_json_v1.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <limits>
#include <map>
#include <mutex>
#include <thread>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace ny::webview {
namespace {
Result failure(const std::string &message) { return {0, {}, "webview: " + message}; }
Result success(std::int64_t value = 1) { return {value, {}, {}}; }
bool valid_text(const std::string &value) {
  if (value.size() > NILWV_MAX_TEXT_BYTES || value.find('\0') != std::string::npos) return false;
  return nilwebview::json_v1::valid_utf8(value);
}
nilwv_text_v1 view(const std::string &value) { return {value.data(), value.size()}; }
std::atomic<std::int64_t> next_handle{1};
std::int64_t fresh_handle() {
  auto value = next_handle.load(std::memory_order_relaxed);
  for (;;) {
    if (value <= 0 || value == std::numeric_limits<std::int64_t>::max()) return 0;
    if (next_handle.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) return value;
  }
}
struct Library {
  const nilwv_api_v1 *api;
  void *module; // Intentionally never unloaded, even after a rejected handshake.
};
// Process-lifetime registry: asynchronous engine callbacks can outlive a
// window/context. Do not let static destructor ordering unload their code.
struct Registry { std::mutex mutex; std::map<std::string, Library> libraries; };
Registry &registry() { static auto *value = new Registry; return *value; }
const nilwv_api_v1 *load_provider(const std::string &path) {
  auto &cache = registry();
  std::lock_guard<std::mutex> lock(cache.mutex);
  auto found = cache.libraries.find(path);
  if (found != cache.libraries.end()) return found->second.api;
  if (cache.libraries.size() >= 16) return nullptr;
  void *module = nullptr;
  nilwv_get_api_v1_fn get_api = nullptr;
#ifdef _WIN32
  auto wide = std::filesystem::u8path(path).wstring();
  auto handle = LoadLibraryExW(wide.c_str(), nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  module = handle;
  if (handle) get_api = reinterpret_cast<nilwv_get_api_v1_fn>(GetProcAddress(handle, "nilwebview_provider_v1_get_api"));
#else
  module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (module) get_api = reinterpret_cast<nilwv_get_api_v1_fn>(dlsym(module, "nilwebview_provider_v1_get_api"));
#endif
  if (!module) return nullptr;
  // Insert before calling foreign code so allocation/validation failure cannot
  // make a loaded library eligible for an accidental later unload.
  auto &entry = cache.libraries.emplace(path, Library{nullptr, module}).first->second;
  const nilwv_api_v1 *api = nullptr;
  try { if (get_api) api = get_api(NILWV_ABI_V1, sizeof(nilwv_api_v1)); } catch (...) { return nullptr; }
  if (!api || api->abi_version != NILWV_ABI_V1 || api->size < sizeof(nilwv_api_v1) ||
      (api->capabilities & ~NILWV_CAP_KNOWN_MASK) ||
      !api->probe || !api->context_create || !api->context_destroy ||
      !api->window || !api->show || !api->navigate || !api->load_html ||
      !api->evaluate_function || !api->poll || !api->event_number ||
      !api->event_text || !api->release_event || !api->close) return nullptr;
  entry.api = api;
  return api;
}
constexpr std::array<const char *, 8> number_fields{
    "window", "kind", "sequence", "operation", "value_kind", "source_kind", "frame_kind", "failure_kind"};
constexpr std::array<const char *, 4> text_fields{"payload_json", "source_url", "source_origin", "detail"};
template <typename Array> std::size_t field_index(const Array &fields, const std::string &key) {
  return static_cast<std::size_t>(std::find(fields.begin(), fields.end(), key) - fields.begin());
}
} // namespace

struct Context::Impl {
  const std::thread::id thread = std::this_thread::get_id();
  const nilwv_api_v1 *api = nullptr;
  nilwv_context_v1 *context = nullptr;
  std::string path, fault;
  std::map<std::int64_t, std::int64_t> windows;
  struct Event { std::array<std::int64_t, 8> numbers{}; std::array<std::string, 4> texts; std::size_t bytes = 0; };
  std::map<std::int64_t, Event> events;
  std::size_t event_bytes = 0;

  Result check(bool configured = true) const {
    if (thread != std::this_thread::get_id()) return failure("context belongs to another thread");
    if (!fault.empty()) return failure(fault);
    if (configured && (!api || !context)) return failure("configure an explicit provider first");
    return success();
  }
  Result protocol() { fault = "provider contract violation; close the execution context"; return failure(fault); }
  template <typename Invoke> Result call(Invoke invoke) {
    nilwv_result_v1 result{};
    result.size = sizeof(result);
    try { invoke(&result); } catch (...) { return protocol(); }
    if (result.size != sizeof(result) || result.code > 0 ||
        result.text.size > NILWV_MAX_TEXT_BYTES || (!result.text.data && result.text.size)) return protocol();
    std::string text;
    if (result.text.size) text.assign(result.text.data, static_cast<std::size_t>(result.text.size));
    if (!valid_text(text)) return protocol();
    if (result.code < 0) return failure(text.empty() ? "provider reported an unspecified failure" : text);
    return {result.number, std::move(text), {}};
  }
  std::int64_t native_window(std::int64_t window) const {
    auto item = windows.find(window); return item == windows.end() ? 0 : item->second;
  }
  template <typename Invoke> Result window_call(std::int64_t window, Invoke invoke, bool operation = false) {
    if (auto state = check(); !state) return state;
    const auto native = native_window(window);
    if (!native) return failure("invalid or closed window handle");
    auto result = call([&](auto *out) { invoke(native, out); });
    if (result && (operation ? result.number <= 0 : result.number != 1)) return protocol();
    return result;
  }
};

Context::Context() : impl_(std::make_unique<Impl>()) {}
Context::~Context() { clear(); }
void Context::clear() noexcept {
  if (!impl_) return;
  // Context ownership cannot be transferred. A foreign-thread teardown must
  // not enter a toolkit on the wrong thread; retained provider state remains
  // pinned, rather than risking callbacks into freed objects.
  if (impl_->thread != std::this_thread::get_id()) return;
  try { if (impl_->context && impl_->api) impl_->api->context_destroy(impl_->context); } catch (...) {}
  impl_->context = nullptr; impl_->api = nullptr;
  impl_->windows.clear(); impl_->events.clear(); impl_->event_bytes = 0;
  impl_->fault.clear(); impl_->path.clear();
}
Result Context::configure(const std::string &path) {
  if (auto state = impl_->check(false); !state) return state;
  if (!valid_text(path) || path.empty() || !std::filesystem::u8path(path).is_absolute())
    return failure("provider path must be absolute UTF-8 without NUL");
  if (impl_->context)
    return path == impl_->path ? success(static_cast<std::int64_t>(impl_->api->capabilities))
                              : failure("this execution already selected a provider");
  const auto *api = load_provider(path);
  if (!api) return failure("provider could not be loaded or has an incompatible ABI");
  auto probe = impl_->call([&](auto *out) { api->probe(out); });
  if (!probe) return probe;
  if (probe.number != 1) return impl_->protocol();
  nilwv_context_v1 *context = nullptr;
  auto created = impl_->call([&](auto *out) { api->context_create(&context, out); });
  if (!created || !context || created.number != 1) {
    try { if (context) api->context_destroy(context); } catch (...) {}
    return !created ? created : impl_->protocol();
  }
  impl_->api = api; impl_->context = context; impl_->path = path;
  return success(static_cast<std::int64_t>(api->capabilities));
}
Result Context::window(const std::string &title, std::int64_t width, std::int64_t height) {
  if (auto state = impl_->check(); !state) return state;
  if (!valid_text(title) || width < 1 || width > 16384 || height < 1 || height > 16384)
    return failure("window requires valid text and dimensions from 1 to 16384");
  if (impl_->windows.size() >= NILWV_MAX_WINDOWS) return failure("window limit reached");
  auto result = impl_->call([&](auto *out) { impl_->api->window(impl_->context, view(title), width, height, out); });
  if (!result) return result;
  if (result.number <= 0 || std::any_of(impl_->windows.begin(), impl_->windows.end(),
      [&](const auto &entry) { return entry.second == result.number; })) return impl_->protocol();
  const auto handle = fresh_handle();
  if (!handle) { impl_->call([&](auto *out) { impl_->api->close(impl_->context, result.number, out); }); return failure("handle space exhausted"); }
  try { impl_->windows.emplace(handle, result.number); }
  catch (...) { impl_->call([&](auto *out) { impl_->api->close(impl_->context, result.number, out); }); throw; }
  return success(handle);
}
Result Context::show(std::int64_t window) {
  return impl_->window_call(window, [&](auto native, auto *out) { impl_->api->show(impl_->context, native, out); });
}
#define NILWV_TEXT_OPERATION(name) \
Result Context::name(std::int64_t window, const std::string &text) { \
  if (!valid_text(text)) return failure("text must be UTF-8 without NUL, at most 1048576 bytes"); \
  return impl_->window_call(window, [&](auto native, auto *out) { impl_->api->name(impl_->context, native, view(text), out); }, true); \
}
NILWV_TEXT_OPERATION(navigate)
NILWV_TEXT_OPERATION(load_html)
NILWV_TEXT_OPERATION(evaluate_function)
#undef NILWV_TEXT_OPERATION

Result Context::poll(std::int64_t window) {
  if (auto state = impl_->check(); !state) return state;
  const auto native = impl_->native_window(window);
  if (!native) return failure("invalid or closed window handle");
  if (impl_->events.size() >= NILWV_MAX_EVENTS) return failure("release retained event snapshots before polling");
  auto polled = impl_->call([&](auto *out) { impl_->api->poll(impl_->context, native, out); });
  if (!polled || polled.number == 0) return polled;
  if (polled.number < 0) return impl_->protocol();
  struct Release {
    Impl *state; std::int64_t handle; bool active = true;
    Result run() { active = false; return state->call([&](auto *out) { state->api->release_event(state->context, handle, out); }); }
    ~Release() { if (active) { try { run(); } catch (...) {} } }
  } release{impl_.get(), polled.number};
  Impl::Event event;
  for (std::size_t i = 0; i < number_fields.size(); ++i) {
    const std::string key = number_fields[i];
    auto field = impl_->call([&](auto *out) { impl_->api->event_number(impl_->context, polled.number, view(key), out); });
    if (!field) return field;
    event.numbers[i] = field.number;
  }
  for (std::size_t i = 0; i < text_fields.size(); ++i) {
    const std::string key = text_fields[i];
    auto field = impl_->call([&](auto *out) { impl_->api->event_text(impl_->context, polled.number, view(key), out); });
    if (!field) return field;
    event.bytes += field.text.size(); event.texts[i] = std::move(field.text);
  }
  const auto &n = event.numbers;
  if (n[0] != native || n[1] < NILWV_EVENT_READY || n[1] > NILWV_EVENT_PROVIDER_FAILED ||
      n[2] <= 0 || n[3] < 0 || n[4] < 0 || n[4] > NILWV_VALUE_UNDEFINED ||
      n[5] < 0 || n[5] > NILWV_SOURCE_NATIVE_ORIGIN || n[6] < 0 || n[6] > NILWV_FRAME_CHILD ||
      n[7] < 0 || n[7] > NILWV_FAILURE_CANCELLED) return impl_->protocol();
  const auto caps = impl_->api->capabilities;
  if ((n[5] == NILWV_SOURCE_UNKNOWN && (!event.texts[1].empty() || !event.texts[2].empty())) ||
      (n[5] == NILWV_SOURCE_NATIVE_URL && (event.texts[1].empty() || !event.texts[2].empty() || !(caps & NILWV_CAP_MESSAGE_URL))) ||
      (n[5] == NILWV_SOURCE_NATIVE_ORIGIN && (event.texts[2].empty() || !(caps & NILWV_CAP_MESSAGE_ORIGIN))) ||
      (!event.texts[1].empty() && !(caps & NILWV_CAP_MESSAGE_URL)) ||
      (!event.texts[2].empty() && !(caps & NILWV_CAP_MESSAGE_ORIGIN)) ||
      (n[6] != NILWV_FRAME_UNKNOWN && !(caps & NILWV_CAP_MESSAGE_FRAME))) return impl_->protocol();
  if (n[4] == NILWV_VALUE_JSON) {
    if (!nilwebview::json_v1::valid_json(event.texts[0])) return impl_->protocol();
  } else if (!event.texts[0].empty()) return impl_->protocol();
  const auto kind = n[1];
  const bool completion = kind >= NILWV_EVENT_NAVIGATION_COMPLETED && kind <= NILWV_EVENT_SCRIPT_FAILED;
  const bool failed = kind == NILWV_EVENT_INITIALIZATION_FAILED || kind == NILWV_EVENT_NAVIGATION_FAILED ||
                      kind == NILWV_EVENT_SCRIPT_FAILED || kind == NILWV_EVENT_PROVIDER_FAILED;
  if ((completion ? n[3] <= 0 : n[3] != 0) || (failed ? n[7] == 0 : n[7] != 0) ||
      (kind != NILWV_EVENT_PAGE_MESSAGE && (n[5] != NILWV_SOURCE_UNKNOWN || n[6] != NILWV_FRAME_UNKNOWN)))
    return impl_->protocol();
  if (kind == NILWV_EVENT_PAGE_MESSAGE || kind == NILWV_EVENT_NAVIGATION_COMPLETED || kind == NILWV_EVENT_NAVIGATION_REQUESTED) {
    if (n[4] != NILWV_VALUE_JSON) return impl_->protocol();
  } else if (kind == NILWV_EVENT_SCRIPT_COMPLETED) {
    if (n[4] != NILWV_VALUE_JSON && n[4] != NILWV_VALUE_UNDEFINED) return impl_->protocol();
  } else if (kind == NILWV_EVENT_NAVIGATION_FAILED) {
    if (n[4] == NILWV_VALUE_UNDEFINED) return impl_->protocol();
  } else if (n[4] != NILWV_VALUE_NONE) return impl_->protocol();
  if ((kind == NILWV_EVENT_NAVIGATION_COMPLETED || kind == NILWV_EVENT_NAVIGATION_REQUESTED ||
       kind == NILWV_EVENT_NAVIGATION_FAILED) && n[4] == NILWV_VALUE_JSON) {
    const auto first = event.texts[0].find_first_not_of(" \t\n\r");
    if (first == std::string::npos || event.texts[0][first] != '"') return impl_->protocol();
  }
  auto released = release.run();
  if (!released || released.number != 1) return impl_->protocol();
  if (event.bytes > NILWV_MAX_QUEUED_BYTES - impl_->event_bytes) {
    impl_->fault = "retained event byte limit exceeded; close the execution context";
    return failure(impl_->fault);
  }
  event.numbers[0] = window;
  const auto handle = fresh_handle();
  if (!handle) return failure("handle space exhausted");
  const auto bytes = event.bytes;
  impl_->events.emplace(handle, std::move(event)); impl_->event_bytes += bytes;
  return success(handle);
}
Result Context::event_number(std::int64_t snapshot, const std::string &field) {
  if (auto state = impl_->check(); !state) return state;
  const auto event = impl_->events.find(snapshot);
  const auto index = field_index(number_fields, field);
  if (event == impl_->events.end()) return failure("invalid or released event handle");
  if (index == number_fields.size()) return failure("unknown numeric event field");
  return success(event->second.numbers[index]);
}
Result Context::event_text(std::int64_t snapshot, const std::string &field) {
  if (auto state = impl_->check(); !state) return state;
  const auto event = impl_->events.find(snapshot);
  const auto index = field_index(text_fields, field);
  if (event == impl_->events.end()) return failure("invalid or released event handle");
  if (index == text_fields.size()) return failure("unknown text event field");
  return {0, event->second.texts[index], {}};
}
Result Context::release_event(std::int64_t snapshot) {
  if (auto state = impl_->check(); !state) return state;
  const auto event = impl_->events.find(snapshot);
  if (event == impl_->events.end()) return failure("invalid or released event handle");
  impl_->event_bytes -= event->second.bytes; impl_->events.erase(event);
  return success();
}
Result Context::close(std::int64_t window) {
  auto result = impl_->window_call(window, [&](auto native, auto *out) { impl_->api->close(impl_->context, native, out); });
  if (!result) return result;
  impl_->windows.erase(window);
  for (auto it = impl_->events.begin(); it != impl_->events.end();) {
    if (it->second.numbers[0] == window) { impl_->event_bytes -= it->second.bytes; it = impl_->events.erase(it); }
    else ++it;
  }
  return success();
}
} // namespace ny::webview
