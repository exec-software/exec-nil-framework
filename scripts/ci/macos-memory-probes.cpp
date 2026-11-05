// Bounded own-child diagnosis, not lifetime-test admission or an SDK guarantee.
// macOS15 XNU: kern_resource.c RLIMIT_AS; vm_map.c size_limit; kern_exec.c.
#include <mach/mach.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>

namespace {
constexpr std::uint64_t MiB = 1024 * 1024;
constexpr std::uint64_t max_cap = 8ULL * 1024 * 1024 * MiB;
constexpr std::size_t block_bytes = 2 * MiB;
constexpr unsigned max_retained = 64;
constexpr std::uint64_t max_reservation = 256 * MiB;
#ifndef MEMORY_PROBE_STARTUP_PAD
#define MEMORY_PROBE_STARTUP_PAD 0
#endif
#if MEMORY_PROBE_STARTUP_PAD
// Referenced by address only. This BSS exercises exec startup map growth without
// touching 192MiB or allocating outside the bounded child controls.
volatile unsigned char startup_pad[MEMORY_PROBE_STARTUP_PAD * MiB];
#endif
struct sample {
  struct rlimit limit{};
  mach_task_basic_info_data_t basic{};
  struct rusage usage{};
  int limit_error = 0;
  int usage_error = 0;
  kern_return_t task_result = KERN_FAILURE;
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  bool good() const { return !limit_error && task_result == KERN_SUCCESS && count == MACH_TASK_BASIC_INFO_COUNT; }
};
sample measure(const char* event) {
  sample row;
  if (getrlimit(RLIMIT_AS, &row.limit)) row.limit_error = errno;
  row.task_result = task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                             reinterpret_cast<task_info_t>(&row.basic), &row.count);
  if (getrusage(RUSAGE_SELF, &row.usage)) row.usage_error = errno;
  std::printf("{\"event\":\"%s\",\"pid\":%d,\"limit_errno\":%d,\"soft\":%" PRIu64
              ",\"hard\":%" PRIu64 ",\"task_result\":%d,\"task_count\":%u,\"expected_task_count\":%u"
              ",\"adjusted_virtual_bytes\":%" PRIu64 ",\"resident_bytes\":%" PRIu64
              ",\"rusage_errno\":%d,\"peak_rss_bytes\":%" PRIu64 ",\"startup_pad_bytes\":%" PRIu64 "}\n",
              event, getpid(), row.limit_error, static_cast<std::uint64_t>(row.limit.rlim_cur),
              static_cast<std::uint64_t>(row.limit.rlim_max), row.task_result, row.count,
              static_cast<unsigned>(MACH_TASK_BASIC_INFO_COUNT), static_cast<std::uint64_t>(row.basic.virtual_size),
              static_cast<std::uint64_t>(row.basic.resident_size), row.usage_error,
              static_cast<std::uint64_t>(row.usage.ru_maxrss), static_cast<std::uint64_t>(MEMORY_PROBE_STARTUP_PAD) * MiB);
#if MEMORY_PROBE_STARTUP_PAD
  std::printf("{\"event\":\"startup-pad-address\",\"address\":\"%p\",\"touched\":false}\n",
              static_cast<const void*>(const_cast<const unsigned char*>(startup_pad)));
#endif
  return row;
}
double now() {
  timespec value{};
  if (clock_gettime(CLOCK_MONOTONIC, &value)) return -1;
  return static_cast<double>(value.tv_sec) + static_cast<double>(value.tv_nsec) / 1e9;
}
bool numeric(const char* text, std::uint64_t& result) {
  if (!text || !*text) return false;
  for (const char* p = text; *p; ++p) if (*p < '0' || *p > '9') return false;
  errno = 0; char* end = nullptr;
  const auto value = std::strtoull(text, &end, 10);
  if (errno || !end || *end || !value || value > max_cap) return false;
  result = value; return true;
}
bool mode_valid(const char* mode) {
  return !std::strcmp(mode, "reserve") || !std::strcmp(mode, "free-loop") ||
         !std::strcmp(mode, "retain-mmap") || !std::strcmp(mode, "retain-calloc");
}
void touch(void* pointer, std::size_t size, std::size_t page, bool full) {
  auto* data = static_cast<volatile unsigned char*>(pointer);
  const auto end = full ? size : (size < page ? size : page);
  for (std::size_t i = 0; i < end; i += page) data[i] = 0x5a;
}
int reservation(const sample& before, std::uint64_t cap, bool uncapped) {
  const auto page = static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
  if (!page || page > MiB) return 23;
  const auto used = static_cast<std::uint64_t>(before.basic.virtual_size);
  const auto headroom = used < cap ? cap - used : 0;
  if (!uncapped && headroom > max_reservation - page) {
    std::printf("{\"event\":\"reservation\",\"outcome\":\"INCONCLUSIVE_BOUND\",\"headroom_estimate\":%" PRIu64 "}\n", headroom);
    return 25;
  }
  const auto bytes = uncapped ? max_reservation : headroom + page;
  errno = 0;
  void* memory = mmap(nullptr, static_cast<std::size_t>(bytes), PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
  const int error = memory == MAP_FAILED ? errno : 0;
  const bool success = memory != MAP_FAILED;
  int cleanup_error = 0;
  if (success && munmap(memory, static_cast<std::size_t>(bytes))) cleanup_error = errno;
  std::printf("{\"event\":\"reservation\",\"bytes\":%" PRIu64 ",\"headroom_estimate\":%" PRIu64
              ",\"succeeded\":%s,\"errno\":%d,\"enomem\":%s,\"munmap_errno\":%d,\"touched_bytes\":0}\n",
              bytes, headroom, success ? "true" : "false", error, error == ENOMEM ? "true" : "false", cleanup_error);
  if (cleanup_error) return 26;
  return uncapped ? (success ? 0 : 24) : (!success && error == ENOMEM ? 0 : 24);
}
int allocations(const char* mode, std::size_t page) {
  const bool reuse = !std::strcmp(mode, "free-loop");
  const bool calloc_mode = !std::strcmp(mode, "retain-calloc");
  void* retained[max_retained]{};
  unsigned count = 0, attempts = 0, freed = 0;
  std::uint64_t touched = 0, cumulative = 0;
  bool rejected = false, cleanup_ok = true, deadline_hit = false;
  int allocation_error = 0;
  const double start = now();
  if (start < 0) return 23;
  const unsigned total = reuse ? 96 : max_retained;
  for (unsigned i = 0; i < total; ++i) {
    const double current = now();
    if (current < 0 || current - start > 12) { deadline_hit = true; break; }
    // Reuse alternates mmap/malloc/calloc; retained controls use one allocator.
    const unsigned allocator = reuse ? i % 3 : (calloc_mode ? 2 : 0);
    errno = 0;
    void* memory = allocator == 0 ? mmap(nullptr, block_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0) :
                   allocator == 1 ? std::malloc(block_bytes) : std::calloc(1, block_bytes);
    ++attempts;
    if (!memory || memory == MAP_FAILED) {
      rejected = true; allocation_error = errno; break;
    }
    touch(memory, block_bytes, page, !reuse);
    cumulative += block_bytes;
    touched += reuse ? page : block_bytes;
    if (reuse) {
      if (allocator == 0) { if (munmap(memory, block_bytes)) cleanup_ok = false; }
      else std::free(memory);
      ++freed;
    } else retained[count++] = memory;
    if (!cleanup_ok) break;
  }
  measure("allocations-before-release");
  for (unsigned i = 0; i < count; ++i) {
    if (calloc_mode) std::free(retained[i]);
    else if (munmap(retained[i], block_bytes)) cleanup_ok = false;
    ++freed;
  }
  std::printf("{\"event\":\"allocations\",\"mode\":\"%s\",\"attempts\":%u,\"retained_blocks\":%u,\"released_blocks\":%u"
              ",\"rejected\":%s,\"errno\":%d,\"enomem\":%s,\"cleanup_ok\":%s,\"deadline_hit\":%s"
              ",\"cumulative_requested_success_bytes\":%" PRIu64 ",\"physical_touch_budget_bytes\":%" PRIu64 "}\n",
              mode, attempts, count, freed, rejected ? "true" : "false", allocation_error,
              allocation_error == ENOMEM ? "true" : "false", cleanup_ok ? "true" : "false", deadline_hit ? "true" : "false", cumulative, touched);
  if (!cleanup_ok) return 26;
  if (deadline_hit) return 27;
  if (reuse) return !rejected && attempts == total ? 0 : 24;
  return rejected && allocation_error == ENOMEM && count > 0 ? 0 : 24;
}
int child(const char* mode, std::uint64_t cap) {
  const auto before = measure("child-start");
  if (!before.good()) return 23;
  if (before.limit.rlim_cur != cap || before.limit.rlim_max != cap) return 22;
  const long page = sysconf(_SC_PAGESIZE);
  if (page <= 0 || page > static_cast<long>(MiB)) return 23;
  const int result = !std::strcmp(mode, "reserve") ? reservation(before, cap, false) : allocations(mode, static_cast<std::size_t>(page));
  const auto after = measure("child-end");
  if (!after.good()) return 23;
  std::printf("{\"event\":\"child-result\",\"mode\":\"%s\",\"result\":%d}\n", mode, result);
  return result;
}
int launch(const char* profile, const char* target, const char* mode) {
  if (target[0] != '/' || !mode_valid(mode)) return 2;
  const auto before = measure("setup-before");
  if (!before.good()) return 23;
  std::uint64_t cap = 0;
  if (!std::strcmp(profile, "fixed96")) cap = 96 * MiB;
  else if (!std::strcmp(profile, "diagnostic-map-plus64")) {
    if (before.basic.virtual_size > max_cap - 64 * MiB) return 23;
    cap = static_cast<std::uint64_t>(before.basic.virtual_size) + 64 * MiB;
  } else return 2;
  struct rlimit requested{static_cast<rlim_t>(cap), static_cast<rlim_t>(cap)};
  errno = 0; const int result = setrlimit(RLIMIT_AS, &requested); const int error = result ? errno : 0;
  std::printf("{\"event\":\"setup-result\",\"profile\":\"%s\",\"mode\":\"%s\",\"cap_bytes\":%" PRIu64
              ",\"setrlimit_result\":%d,\"errno\":%d}\n", profile, mode, cap, result, error);
  if (result) return 20;
  const auto after = measure("setup-after");
  if (!after.good() || after.limit.rlim_cur != cap || after.limit.rlim_max != cap) return 22;
  char cap_text[32]{}; std::snprintf(cap_text, sizeof(cap_text), "%" PRIu64, cap);
  char* const arguments[] = {const_cast<char*>(target), const_cast<char*>("--child"), const_cast<char*>(mode), cap_text, nullptr};
  std::printf("{\"event\":\"exec-start\",\"pid\":%d}\n", getpid());
  std::fflush(stdout);
  execv(target, arguments);
  std::printf("{\"event\":\"exec-failed\",\"errno\":%d}\n", errno);
  return 21;
}
}
int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc == 2 && !std::strcmp(argv[1], "--observe")) return measure("baseline").good() ? 0 : 23;
  if (argc == 2 && !std::strcmp(argv[1], "--uncapped")) {
    const auto before = measure("uncapped-start");
    if (!before.good()) return 23;
    const int result = reservation(before, 0, true); measure("uncapped-end"); return result;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--wait")) {
    measure("wait-started"); for (int i = 0; i < 30; ++i) sleep(1); return 0;
  }
  if (argc == 5 && !std::strcmp(argv[1], "--launch")) return launch(argv[2], argv[3], argv[4]);
  if (argc == 4 && !std::strcmp(argv[1], "--child") && mode_valid(argv[2])) {
    std::uint64_t cap = 0; if (!numeric(argv[3], cap)) return 2; return child(argv[2], cap);
  }
  std::fprintf(stderr, "usage: memory-probe --observe|--uncapped|--wait|--launch PROFILE TARGET MODE|--child MODE CAP\n");
  return 2;
}
