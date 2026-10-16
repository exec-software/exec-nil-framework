#include "matrix_hag_bridge.hpp"

#include <cstdlib>
#include <mutex>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace matrix_hag {
namespace {

// Signatures from exec_hag.h. Kept here rather than including the header so
// the NIL runtime builds with no HAG checkout present.
using open_fn = int (*)(const char *, const unsigned char *const *,
                        const std::uint32_t *, std::uint32_t,
                        const std::uint32_t *, std::uint32_t, std::uint32_t,
                        std::uint64_t, void **);
using write_fn = int (*)(void *, std::uint32_t, std::uint64_t,
                         const unsigned char *, std::uint64_t);
using read_fn = int (*)(void *, std::uint32_t, std::uint64_t, unsigned char *,
                        std::uint64_t);
using dispatch_fn = int (*)(void *, std::uint32_t, const std::uint64_t *,
                            const std::uint32_t *, const std::uint32_t *,
                            std::uint32_t);
using allocation_fn = std::uint64_t (*)(const void *);
using poisoned_fn = int (*)(const void *);
using close_fn = int (*)(void *);
using text_fn = const char *(*)(int);
using vgprs_fn = std::uint32_t (*)();

struct library {
  void *handle = nullptr;
  open_fn open = nullptr;
  write_fn write = nullptr;
  read_fn read = nullptr;
  dispatch_fn dispatch = nullptr;
  allocation_fn allocation_bytes = nullptr;
  poisoned_fn is_poisoned = nullptr;
  close_fn close = nullptr;
  text_fn error_text = nullptr;
  vgprs_fn min_vgprs = nullptr;
  vgprs_fn max_vgprs = nullptr;
  vgprs_fn max_groups = nullptr;
  bool ready = false;
  std::string reason;
};

void *open_library(const char *name) {
#if defined(_WIN32)
  return reinterpret_cast<void *>(LoadLibraryA(name));
#else
  // RTLD_LOCAL so HAG's symbols do not join the global namespace; the bridge
  // is the only thing that may reach them.
  return dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
}

void *find_symbol(void *handle, const char *name) {
#if defined(_WIN32)
  return reinterpret_cast<void *>(
      GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
  return dlsym(handle, name);
#endif
}

std::string load_error() {
#if defined(_WIN32)
  return "LoadLibrary failed";
#else
  const char *text = dlerror();
  return text == nullptr ? std::string("unknown dynamic loader error")
                         : std::string(text);
#endif
}

library &instance() {
  static library state;
  static std::once_flag once;
  std::call_once(once, [] {
    // An explicit path wins. Otherwise use the platform's ordinary search;
    // the bridge never goes looking through the filesystem itself.
    const char *configured = std::getenv("NY_HAG_LIBRARY");
    const char *name = configured != nullptr && configured[0] != '\0'
                           ? configured
                           : "libexec_hag.so";
    state.handle = open_library(name);
    if (state.handle == nullptr) {
      state.reason = std::string("cannot load '") + name + "': " + load_error();
      return;
    }
    const auto bind = [&](auto &slot, const char *symbol) {
      slot = reinterpret_cast<std::decay_t<decltype(slot)>>(
          find_symbol(state.handle, symbol));
      if (slot == nullptr && state.reason.empty())
        state.reason = std::string("missing symbol '") + symbol + "'";
    };
    bind(state.open, "hag_queue_open");
    bind(state.write, "hag_queue_write");
    bind(state.read, "hag_queue_read");
    bind(state.dispatch, "hag_queue_dispatch");
    bind(state.allocation_bytes, "hag_queue_allocation_bytes");
    bind(state.is_poisoned, "hag_queue_is_poisoned");
    bind(state.close, "hag_queue_close");
    bind(state.error_text, "hag_error_text");
    bind(state.min_vgprs, "hag_min_kernel_vgprs");
    bind(state.max_vgprs, "hag_max_kernel_vgprs");
    bind(state.max_groups, "hag_max_dispatch_groups");
    state.ready = state.reason.empty();
  });
  return state;
}

void *as_queue(std::int64_t handle) {
  return reinterpret_cast<void *>(static_cast<std::uintptr_t>(handle));
}

} // namespace

bool available() { return instance().ready; }

const std::string &unavailable_reason() { return instance().reason; }

std::string status_text(int code) {
  switch (code) {
  case bridge_unavailable:
    return "HAG is not available: " + unavailable_reason();
  case bridge_bad_pointer:
    return "buffer pointer does not refer to a live allocation";
  case bridge_out_of_bounds:
    return "region lies outside the allocation it was taken from";
  case bridge_not_byte_backed:
    return "handle holds element values rather than bytes; load the image "
           "with ny.lang.io.file.read_bytes or build it in a raw byte buffer";
  default:
    break;
  }
  auto &state = instance();
  if (!state.ready || state.error_text == nullptr)
    return "unknown status " + std::to_string(code);
  const char *text = state.error_text(code);
  return text == nullptr ? std::string("unknown status") : std::string(text);
}

int queue_open(const std::string &device_path, const unsigned char *program,
               std::uint32_t program_bytes, std::uint32_t slot0,
               std::uint32_t slot1, std::uint32_t slot2, std::uint32_t vgprs,
               std::uint32_t max_groups, std::uint64_t budget_bytes,
               std::int64_t *out_handle) {
  auto &state = instance();
  if (!state.ready)
    return bridge_unavailable;
  const unsigned char *programs[1] = {program};
  const std::uint32_t sizes[1] = {program_bytes};
  const std::uint32_t slots[3] = {slot0, slot1, slot2};
  void *queue = nullptr;
  const int status = state.open(device_path.c_str(), programs, sizes, 1, slots,
                                vgprs, max_groups, budget_bytes, &queue);
  if (status == hag_ok && out_handle != nullptr)
    *out_handle = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(queue));
  return status;
}

int queue_write(std::int64_t handle, std::uint32_t slot, std::uint64_t offset,
                const unsigned char *bytes, std::uint64_t length) {
  auto &state = instance();
  if (!state.ready)
    return bridge_unavailable;
  return state.write(as_queue(handle), slot, offset, bytes, length);
}

int queue_read(std::int64_t handle, std::uint32_t slot, std::uint64_t offset,
               unsigned char *bytes, std::uint64_t length) {
  auto &state = instance();
  if (!state.ready)
    return bridge_unavailable;
  return state.read(as_queue(handle), slot, offset, bytes, length);
}

int queue_dispatch(std::int64_t handle, std::uint32_t program,
                   const std::uint64_t offsets[3], const std::uint32_t lengths[3],
                   const std::uint32_t scalars[2], std::uint32_t groups) {
  auto &state = instance();
  if (!state.ready)
    return bridge_unavailable;
  return state.dispatch(as_queue(handle), program, offsets, lengths, scalars,
                        groups);
}

std::uint64_t queue_allocation_bytes(std::int64_t handle) {
  auto &state = instance();
  return state.ready ? state.allocation_bytes(as_queue(handle)) : 0;
}

int queue_is_poisoned(std::int64_t handle) {
  auto &state = instance();
  return state.ready ? state.is_poisoned(as_queue(handle)) : 1;
}

int queue_close(std::int64_t handle) {
  auto &state = instance();
  if (!state.ready)
    return bridge_unavailable;
  return state.close(as_queue(handle));
}

std::uint32_t min_kernel_vgprs() {
  auto &state = instance();
  return state.ready ? state.min_vgprs() : 0;
}

std::uint32_t max_kernel_vgprs() {
  auto &state = instance();
  return state.ready ? state.max_vgprs() : 0;
}

std::uint32_t max_dispatch_groups() {
  auto &state = instance();
  return state.ready ? state.max_groups() : 0;
}

} // namespace matrix_hag
