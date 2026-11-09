/*
 * Copyright (C) 2008 - 2026 Execute Software. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
// The native runtime ABI for a target with no operating system.
//
// An object emitted by `nilassembler --freestanding` imports exactly five
// symbols -- nyrt_abi_version, nyrt_context_begin, nyrt_context_end,
// nyrt_fail and nyrt_profile_site -- and nothing else. This translation unit
// supplies them without calling into libc, a kernel, or an allocator, so a
// freestanding host can link it instead of reimplementing the boundary or
// satisfying it with stubs that fail if reached.
//
// What it deliberately does not do: allocate, format, read a clock, or touch
// a device. Those belong to a host. nyrt_fail reports through a weak hook the
// host may define; the default stops the caller rather than returning into a
// program whose invariants have already been broken.
//
// pinned: this file must not gain an #include of a hosted header. The
// freestanding link test asserts the resulting archive imports nothing.
#include <cstdint>

namespace {

// Recorded for a host that wants them; nothing here interprets them.
int entry_argc = 0;
const char *const *entry_argv = nullptr;
bool context_active = false;

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

// Reads what the entry recorded. Not part of the five imported symbols; a
// host links this only if it wants the values.
int __ny_entry_argc() noexcept { return entry_argc; }
const char *const *__ny_entry_argv() noexcept { return entry_argv; }
int __ny_context_active() noexcept { return context_active ? 1 : 0; }

} // extern "C"
