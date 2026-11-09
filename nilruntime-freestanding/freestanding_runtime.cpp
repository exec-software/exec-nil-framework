/*
 * Copyright (C) 2008 - 2026 Execute Software. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
// The native runtime ABI for a target with no operating system.
//
// An object emitted by `nilassembler --freestanding` imports five symbols --
// nyrt_abi_version, nyrt_context_begin, nyrt_context_end, nyrt_fail and
// nyrt_profile_site -- plus, only if the program allocates, nyrt_alloc,
// nyrt_mem_copy and nyrt_str_length. It imports nothing else, and no libc at
// all. This translation unit supplies all of them without calling into libc
// or a kernel, so a freestanding host can link it instead of reimplementing
// the boundary or satisfying it with stubs that fail if reached.
//
// What it deliberately does not do: own memory, format, read a clock, or
// touch a device. Those belong to a host. nyrt_fail reports through a weak
// hook the host may define; the default stops the caller rather than
// returning into a program whose invariants have already been broken.
//
// Allocation is the one case where generated code needs memory and this
// translation unit must answer. The answer is mechanism without policy: the
// host owns the memory and installs it with nyrt_heap_init, and nyrt_alloc
// bumps a pointer through that region. Nothing here declares a static heap,
// because the size of a vessel's memory is the vessel's decision and a fixed
// array would charge every consumer for it. With no region installed
// nyrt_alloc returns null, which generated code already treats as a failure
// and reports through nyrt_fail -- so a program that never allocates never
// needs a region, and one that does cannot silently proceed without it.
// There is no free: a bump region is released as a whole, and
// nyrt_context_end returns it to empty.
//
// There is no locking. A region is a single bump pointer and two concurrent
// allocations would race, which is correct for the targets this serves --
// nothing freestanding is emitted with threads -- and is a limit a host
// should know rather than discover.
//
// pinned: this file must not gain an #include of a hosted header. That is
// what the freestanding link test can check, by asserting the resulting
// archive imports nothing but the weak failure hook. That it declares no heap
// of its own is a rule, not a test: a static array here would still link.
#include <cstdint>

namespace {

// Recorded for a host that wants them; nothing here interprets them.
int entry_argc = 0;
const char *const *entry_argv = nullptr;
bool context_active = false;

// The host's region, installed by nyrt_heap_init. Null until then, which is
// the state in which allocation fails rather than inventing memory.
unsigned char *heap_base = nullptr;
std::uint64_t heap_size = 0;
std::uint64_t heap_used = 0;

// Every allocation starts at this boundary, which is the strictest alignment
// a scalar on the supported targets requires. Generated code receives raw
// bytes and may store any type into them.
constexpr std::uint64_t heap_alignment = 16;

} // namespace

extern "C" {

// A host that can report a failure defines this. The default below is weak so
// linking the archive alone still produces a complete program.
__attribute__((weak)) void __ny_host_fail(const char *message) noexcept;

// Must equal the version the generated entry checks for; a mismatch makes the
// entry call nyrt_fail before running any user code.
std::uint32_t nyrt_abi_version() noexcept { return 1; }

void nyrt_context_begin(int argc, const char *const *argv) noexcept {
  entry_argc = argc;
  entry_argv = argv;
  context_active = true;
}

void nyrt_context_end() noexcept {
  context_active = false;
  entry_argc = 0;
  entry_argv = nullptr;
  // The region belongs to the host and may outlive the context, but nothing
  // allocated inside it does: every pointer handed out came from generated
  // code whose lifetime ended with the context.
  //
  // Only the pointer is reset. The bytes are deliberately left, because the
  // region is the host's and what a program wrote into it is the host's to
  // read once the program has returned -- for many freestanding hosts that is
  // the only channel out of a Ny object, which exports nothing but its entry.
  // A host that needs them gone clears the region, since only it knows
  // whether they were sensitive.
  heap_used = 0;
}

// Profiling attribution is a hosted capture facility. Accepting and ignoring
// the call keeps generated code identical across hosted and freestanding
// builds, which is the point of the boundary.
void nyrt_profile_site(const char *file, const char *function,
                       std::uint64_t line) noexcept {
  (void)file;
  (void)function;
  (void)line;
}

[[noreturn]] void nyrt_fail(const char *message) noexcept {
  if (__ny_host_fail) __ny_host_fail(message);
  // No host reporter, or one that returned anyway: a failed program must not
  // continue. Stop here rather than unwinding into broken state.
  for (;;) {
  }
}

// Installs the region allocation draws from. Exported rather than imported:
// a host calls this, it does not have to implement anything. Calling it again
// replaces the region and discards what was handed out of the old one, so a
// host that reinstalls must be done with the previous contents.
void nyrt_heap_init(void *base, std::uint64_t size) noexcept {
  heap_base = static_cast<unsigned char *>(base);
  heap_size = base ? size : 0;
  heap_used = 0;
}

// Bumps through the host's region. Returns null when no region is installed
// or the request does not fit, which is the same answer a hosted allocator
// gives under pressure and which generated code already checks.
void *nyrt_alloc(std::uint64_t size) noexcept {
  if (!heap_base) return nullptr;
  // A zero-sized request still has to return a distinct address, so it is
  // served as one alignment unit rather than aliasing the next allocation.
  if (size == 0) size = 1;
  const std::uint64_t padding =
      (heap_alignment - (heap_used % heap_alignment)) % heap_alignment;
  if (padding > heap_size - heap_used) return nullptr;
  const std::uint64_t start = heap_used + padding;
  // Checked against the space left rather than by adding, so a size near the
  // top of the range cannot wrap past the test that is meant to catch it.
  if (size > heap_size - start) return nullptr;
  unsigned char *const block = heap_base + start;
  heap_used = start + size;
  // Zeroed because the helper this serves replaced calloc, and generated code
  // is entitled to assume a fresh block holds no earlier vessel's bytes.
  for (std::uint64_t index = 0; index < size; ++index) block[index] = 0;
  return block;
}

// Byte copy and string length, supplied here because a freestanding object
// has no libc to call and these are not the host's decision to make.
void *nyrt_mem_copy(void *destination, const void *source,
                    std::uint64_t size) noexcept {
  unsigned char *out = static_cast<unsigned char *>(destination);
  const unsigned char *in = static_cast<const unsigned char *>(source);
  for (std::uint64_t index = 0; index < size; ++index) out[index] = in[index];
  return destination;
}

std::uint64_t nyrt_str_length(const char *text) noexcept {
  if (!text) return 0;
  std::uint64_t length = 0;
  while (text[length] != '\0') ++length;
  return length;
}

// Reads what the entry recorded. Not part of the five imported symbols; a
// host links this only if it wants the values.
int __ny_entry_argc() noexcept { return entry_argc; }
const char *const *__ny_entry_argv() noexcept { return entry_argv; }
int __ny_context_active() noexcept { return context_active ? 1 : 0; }

// How much of the region has been handed out. For a host that wants to see
// whether a vessel allocated at all, or how close it came to its bound.
std::uint64_t nyrt_heap_used() noexcept { return heap_used; }
std::uint64_t nyrt_heap_size() noexcept { return heap_size; }

} // extern "C"
