#include "memory_profile.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ny::memory_profile {
namespace {
constexpr std::size_t maximum_sites = 4096;
constexpr std::size_t maximum_live_objects = 1024 * 1024;
std::atomic<bool> capture_claimed{false};
std::atomic<std::uint64_t> excluded_sessions{0};

void diagnostic(const char *message) noexcept {
  std::fprintf(stderr, "Aperture memory profile error: %s\n", message);
  std::fflush(stderr);
}

void add(std::uint64_t &value, std::uint64_t amount) noexcept {
  value += std::min(amount, (std::numeric_limits<std::uint64_t>::max)() - value);
}

struct Counters {
  std::uint64_t allocated_bytes = 0, allocated_objects = 0;
  std::uint64_t live_bytes = 0, live_objects = 0;
  std::uint64_t peak_bytes = 0, peak_objects = 0;
  std::uint64_t freed_bytes = 0, freed_objects = 0, resizes = 0;
  void increase(std::uint64_t bytes, bool object) noexcept {
    add(allocated_bytes, bytes);
    add(live_bytes, bytes);
    if (object) { add(allocated_objects, 1); add(live_objects, 1); }
    peak_bytes = std::max(peak_bytes, live_bytes);
    peak_objects = std::max(peak_objects, live_objects);
  }
  void decrease(std::uint64_t bytes, bool object) noexcept {
    add(freed_bytes, bytes);
    live_bytes -= std::min(live_bytes, bytes);
    if (object) { add(freed_objects, 1); if (live_objects) --live_objects; }
  }
};

// Escape arbitrary bytes as valid JSON. UTF-8 remains readable; malformed
// UTF-8 is escaped byte-by-byte, rather than emitting an invalid document.
void quoted(std::FILE *file, std::string_view value) noexcept {
  std::fputc('"', file);
  for (std::size_t i = 0; i < value.size(); ++i) {
    const auto ch = static_cast<unsigned char>(value[i]);
    if (ch == '"' || ch == '\\') { std::fputc('\\', file); std::fputc(ch, file); }
    else if (ch < 0x20 || ch == 0x7f) std::fprintf(file, "\\u%04x", ch);
    else if (ch < 0x80) std::fputc(ch, file);
    else {
      const std::size_t width = ch >= 0xc2 && ch <= 0xdf ? 2 :
          ch >= 0xe0 && ch <= 0xef ? 3 : ch >= 0xf0 && ch <= 0xf4 ? 4 : 0;
      bool valid = width != 0 && width <= value.size() - i;
      for (std::size_t j = 1; valid && j < width; ++j) {
        const auto next = static_cast<unsigned char>(value[i + j]);
        valid = next >= 0x80 && next <= 0xbf;
      }
      if (valid && width >= 3) {
        const auto next = static_cast<unsigned char>(value[i + 1]);
        valid = !((ch == 0xe0 && next < 0xa0) || (ch == 0xed && next >= 0xa0) ||
                  (ch == 0xf0 && next < 0x90) || (ch == 0xf4 && next >= 0x90));
      }
      if (valid) { std::fwrite(value.data() + i, 1, width, file); i += width - 1; }
      else std::fprintf(file, "\\u%04x", ch);
    }
  }
  std::fputc('"', file);
}

void counters(std::FILE *file, const Counters &value) noexcept {
  std::fprintf(file,
      "\"allocated_bytes\":%llu,\"allocated_objects\":%llu,"
      "\"live_bytes\":%llu,\"live_objects\":%llu,"
      "\"peak_bytes\":%llu,\"peak_objects\":%llu,"
      "\"freed_bytes\":%llu,\"freed_objects\":%llu,\"resizes\":%llu",
      static_cast<unsigned long long>(value.allocated_bytes),
      static_cast<unsigned long long>(value.allocated_objects),
      static_cast<unsigned long long>(value.live_bytes),
      static_cast<unsigned long long>(value.live_objects),
      static_cast<unsigned long long>(value.peak_bytes),
      static_cast<unsigned long long>(value.peak_objects),
      static_cast<unsigned long long>(value.freed_bytes),
      static_cast<unsigned long long>(value.freed_objects),
      static_cast<unsigned long long>(value.resizes));
}

std::FILE *reserve_file(const char *path) noexcept {
#ifdef _WIN32
  const int descriptor = ::_open(path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                                _S_IREAD | _S_IWRITE);
  if (descriptor < 0) return nullptr;
  auto *file = ::_fdopen(descriptor, "wb");
  if (!file) ::_close(descriptor);
#else
  const int descriptor = ::open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (descriptor < 0) return nullptr;
  auto *file = ::fdopen(descriptor, "wb");
  if (!file) ::close(descriptor);
#endif
  return file;
}
} // namespace

struct Session::State {
  using Key = std::tuple<std::string, std::string, std::uint64_t, std::string>;
  struct Site { Key key; Counters counts; };
  struct Allocation { std::size_t site; std::uint64_t bytes; };
  std::FILE *file = nullptr;
  bool active = true, failed = false, site_limit_reached = false;
  const char *error = nullptr;
  std::string route, current_file, current_function;
  std::uint64_t current_line = 0;
  Counters totals;
  std::uint64_t collections = 0, collection_ns = 0, collected_bytes = 0, collected_objects = 0;
  std::vector<Site> sites;
  std::map<Key, std::size_t> site_index;
  std::unordered_map<const void *, Allocation> allocations;

  void fail(const char *message) noexcept {
    if (!failed) diagnostic(message);
    failed = true;
    error = message;
  }
  std::size_t current_site(std::string_view kind) {
    Key key{current_file, current_function, current_line, std::string(kind.substr(0, 64))};
    auto found = site_index.find(key);
    if (found != site_index.end()) return found->second;
    if (sites.size() >= maximum_sites) {
      site_limit_reached = true;
      key = Key{"", "<site limit reached>", 0, "other"};
      found = site_index.find(key);
      if (found != site_index.end()) return found->second;
    }
    const auto index = sites.size();
    site_index.emplace(key, index);
    sites.push_back({std::move(key), {}});
    return index;
  }
};

Session::Session() noexcept = default;
Session::~Session() { (void)finish("abandoned", false); }

bool Session::begin(std::string_view route) noexcept {
  if (state_ && state_->active) return true;
  const char *path = std::getenv("NIL_MEMORY_PROFILE");
  if (!path || !*path) return true;
  if (capture_claimed.exchange(true)) { ++excluded_sessions; return true; }
  try {
    state_ = std::make_unique<State>();
    state_->route = std::string(route.substr(0, 64));
    state_->file = reserve_file(path);
    if (!state_->file) {
      state_->active = false;
      diagnostic("could not create capture destination (it must be a new writable file)");
      return false;
    }
    return true;
  } catch (...) {
    if (state_) state_->active = false;
    diagnostic("could not initialize capture state");
    return false;
  }
}

bool Session::enabled() const noexcept { return state_ && state_->active; }

void Session::site(std::string_view file, std::string_view function, std::uint64_t line) noexcept {
  if (!enabled() || state_->failed) return;
  try {
    state_->current_file.assign(file.substr(0, 4096));
    state_->current_function.assign(function.substr(0, 1024));
    state_->current_line = line;
  } catch (...) { state_->fail("could not retain source attribution"); }
}

void Session::allocate(const void *identity, std::uint64_t bytes, std::string_view kind) noexcept {
  if (!enabled() || state_->failed || !identity) return;
  try {
    if (state_->allocations.size() >= maximum_live_objects) {
      state_->fail("capture exceeded the live allocation tracking limit"); return;
    }
    if (state_->allocations.find(identity) != state_->allocations.end()) {
      state_->fail("duplicate live allocation identity"); return;
    }
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (bytes > maximum - state_->totals.allocated_bytes ||
        state_->totals.allocated_objects == maximum) {
      state_->fail("capture counters exceeded their representable range"); return;
    }
    const auto site = state_->current_site(kind);
    state_->allocations.emplace(identity, State::Allocation{site, bytes});
    state_->totals.increase(bytes, true);
    state_->sites[site].counts.increase(bytes, true);
  } catch (...) { state_->fail("could not retain allocation event"); }
}

void Session::resize(const void *identity, std::uint64_t bytes) noexcept {
  if (!enabled() || state_->failed) return;
  const auto found = state_->allocations.find(identity);
  if (found == state_->allocations.end()) return;
  auto &allocation = found->second;
  auto &site = state_->sites[allocation.site].counts;
  if (bytes >= allocation.bytes && bytes - allocation.bytes >
      (std::numeric_limits<std::uint64_t>::max)() - state_->totals.allocated_bytes) {
    state_->fail("capture counters exceeded their representable range"); return;
  }
  add(state_->totals.resizes, 1); add(site.resizes, 1);
  if (bytes >= allocation.bytes) {
    state_->totals.increase(bytes - allocation.bytes, false);
    site.increase(bytes - allocation.bytes, false);
  } else {
    state_->totals.decrease(allocation.bytes - bytes, false);
    site.decrease(allocation.bytes - bytes, false);
  }
  allocation.bytes = bytes;
}

void Session::release(const void *identity) noexcept {
  if (!enabled() || state_->failed) return;
  const auto found = state_->allocations.find(identity);
  if (found == state_->allocations.end()) return;
  state_->totals.decrease(found->second.bytes, true);
  state_->sites[found->second.site].counts.decrease(found->second.bytes, true);
  state_->allocations.erase(found);
}

void Session::collection(std::uint64_t duration_ns, std::uint64_t freed_bytes,
                         std::uint64_t freed_objects) noexcept {
  if (!enabled() || state_->failed) return;
  add(state_->collections, 1); add(state_->collection_ns, duration_ns);
  add(state_->collected_bytes, freed_bytes); add(state_->collected_objects, freed_objects);
}

bool Session::finish(std::string_view status, bool completed) noexcept {
  if (!enabled()) return true;
  auto &state = *state_;
  state.active = false;
  auto *file = state.file;
  state.file = nullptr;
  std::fputs("{\"schema\":\"aperture.memory.v1\",\"route\":", file);
  quoted(file, state.route);
  std::fprintf(file, ",\"completed\":%s,\"status\":", completed && !state.failed ? "true" : "false");
  quoted(file, state.failed ? std::string_view("profile_error") : status);
  std::fputs(",\"measurement\":\"runtime_logical_bytes\",\"totals\":{", file);
  counters(file, state.totals);
  std::fprintf(file, "},\"collections\":{\"count\":%llu,\"duration_ns\":%llu,\"freed_bytes\":%llu,\"freed_objects\":%llu},\"sites\":[",
      static_cast<unsigned long long>(state.collections), static_cast<unsigned long long>(state.collection_ns),
      static_cast<unsigned long long>(state.collected_bytes), static_cast<unsigned long long>(state.collected_objects));
  for (std::size_t i = 0; i < state.sites.size(); ++i) {
    const auto &site = state.sites[i];
    if (i) std::fputc(',', file);
    std::fputs("{\"file\":", file); quoted(file, std::get<0>(site.key));
    std::fputs(",\"function\":", file); quoted(file, std::get<1>(site.key));
    std::fprintf(file, ",\"line\":%llu,\"kind\":", static_cast<unsigned long long>(std::get<2>(site.key)));
    quoted(file, std::get<3>(site.key)); std::fputc(',', file); counters(file, site.counts); std::fputc('}', file);
  }
  std::fprintf(file, "],\"excluded_sessions\":%llu,\"site_limit_reached\":%s,\"scope\":[",
      static_cast<unsigned long long>(excluded_sessions.load()), state.site_limit_reached ? "true" : "false");
  quoted(file, "Live allocations are a snapshot before runtime teardown; retained does not mean leaked.");
  std::fputc(',', file); quoted(file, "Runtime logical accounting is not process RSS; allocator overhead, external libraries and profiler storage are excluded.");
  std::fputc(',', file); quoted(file, "Only the first opted-in runtime session is captured; parallel workers and later sessions are excluded.");
  std::fputc(',', file); quoted(file, "Source sites are bounded to 4096 distinct entries plus an overflow entry; growth remains attributed to the original allocation site.");
  std::fputc(',', file);
  if (state.route == "vm")
    quoted(file, "VM coverage is typed array, raw buffer and iterator payload bytes; Value storage, vector capacity, maps, text and record storage are excluded.");
  else
    quoted(file, "Native coverage is runtime-managed allocations, array descriptors/capacity and runtime buffer descriptors/guards; unmanaged instructions that allocate directly through libc are excluded. Logically freed buffer tombstones are excluded even while retained for stale-pointer protection.");
  std::fputs("],\"errors\":[", file);
  if (state.error) quoted(file, state.error);
  std::fputs("]}\n", file);
  bool ok = std::ferror(file) == 0;
  if (std::fclose(file) != 0) ok = false;
  if (!ok) diagnostic("could not finish writing capture destination");
  return ok && !state.failed;
}
} // namespace ny::memory_profile
