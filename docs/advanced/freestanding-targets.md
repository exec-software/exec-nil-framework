# Freestanding native targets

A **freestanding** Ny object is one emitted for a target with no C library,
no operating-system services and no host allocator: a kernel, a bootloader, or
a userspace process on a system that supplies its own entry and memory. This
page is the contract such a consumer links against. It exists so that
consuming a Ny object is a matter of reading a document rather than inspecting
the emitted code and implementing what it appears to need.

Freestanding targets are a **capability, not a qualified release target**. The
[v1 release profile](../release/v1-profile.md) excludes them deliberately, and
that exclusion is unchanged by this page. The hook names and signatures below
are version 1 of this boundary, reported by `nyrt_abi_version`; they may change
while the release candidate is open.

## Emitting

```sh
nilcompiler --no-stdlib --no-bundle --unmanaged -o program.nil program.ny
nilassembler --unmanaged --freestanding --no-red-zone -o program.o program.nil
```

| Option | Effect |
| --- | --- |
| `--freestanding` | Emits the entry as `__ny_entry()` and defines no C `main`. |
| `--no-red-zone` | Marks every generated definition `noredzone`. |
| `--emit-llvm <path>` | Writes the generated LLVM IR, so an attribute that does not survive into an object can still be inspected. |

The three are independent. `--no-red-zone` is separate from `--freestanding`
because they answer different questions: a target whose interrupt entry
switches stacks can keep the red zone in its userspace code, while a
kernel-reentrant one cannot, and that is the consumer's call rather than a
consequence of having no libc.

## Entry

A freestanding object exports `__ny_entry` and no `main`:

```c
long __ny_entry(void);   // returns what Ny's main returned
```

It takes no arguments. The host supplies the process or task entry — there is
no C runtime to call `__ny_entry` for it — and interprets the return value.
Argument-vector derivation is not emitted at all for this target, rather than
emitted and fed zeroes, so nothing in the object reaches for an environment
that does not exist.

`__ny_entry` performs, in order:

1. `nyrt_abi_version()`, and `nyrt_fail` if it is not the expected version.
   This happens before any user code runs.
2. `nyrt_context_begin(0, NULL)`.
3. Global initialisation.
4. The program's `main`.
5. `nyrt_context_end()`.
6. Return.

## The hooks a host must satisfy

Five, always:

```c
uint32_t nyrt_abi_version(void);
void     nyrt_context_begin(int argc, const char *const *argv);
void     nyrt_context_end(void);
void     nyrt_profile_site(const char *file, const char *function, uint64_t line);
void     nyrt_fail(const char *message);   // must not return
```

`nyrt_fail` receives a message describing what went wrong and must not return
into the program: its invariants are already broken. `nyrt_profile_site` is an
attribution point for a hosted capture facility; accepting and ignoring the
call is correct on a target that has none, and keeps generated code identical
across hosted and freestanding builds.

Three more, **only if the program allocates** — today, only if it
concatenates a string:

```c
void     *nyrt_alloc(uint64_t size);                              // zeroed, NULL on failure
void     *nyrt_mem_copy(void *destination, const void *source, uint64_t size);
uint64_t  nyrt_str_length(const char *text);
```

These replace `calloc`, `memcpy` and `strlen`. A freestanding object imports no
libc symbol under any configuration; an object for a program that never
allocates imports only the first five names. The helper that needs them is
built on first use, so the import surface reflects what the program does.

`nyrt_alloc` returns zeroed memory and `NULL` when it cannot satisfy the
request. Generated code checks for `NULL` and reports it through `nyrt_fail`,
so an allocator with nothing to give fails the program rather than being
allowed to lie.

## The runtime NIL ships

`nilruntime_freestanding` (`libnilruntime_freestanding.a`) implements all
eight hooks with no libc, no kernel and no heap of its own. A consumer may
link it instead of writing the boundary. It does not allocate on its own
behalf, format, read a clock or touch a device.

Memory is the one place where generated code needs something the runtime
cannot invent, and the division is **the host owns the memory, the runtime
owns the mechanism**:

```c
void     nyrt_heap_init(void *base, uint64_t size);  // install a region
uint64_t nyrt_heap_used(void);                       // how much is handed out
uint64_t nyrt_heap_size(void);                       // the installed size
```

The host calls `nyrt_heap_init` with storage it owns; `nyrt_alloc` bumps a
16-byte-aligned pointer through that region and zeroes what it hands out.
There is deliberately no static heap in the runtime, because the size of a
vessel's memory is the vessel's decision and a fixed array would charge every
consumer for it.

Consequences worth knowing before depending on it:

- **There is no free.** A bump region is released as a whole. `nyrt_context_end`
  returns it to empty, which is sound because everything allocated inside it
  came from generated code whose lifetime ended with the context.
- **Ending the context resets the pointer; it does not scrub the bytes.** The
  region belongs to the host, so what is written in it stays the host's to
  read after `__ny_entry` returns. A host that wants the bytes a program
  produced reads them then; a host that wants them gone clears the region
  itself, because only the host knows whether they were sensitive.
- **With no region installed, allocation fails.** `nyrt_alloc` returns `NULL`
  and the program stops through `nyrt_fail`. A program that never allocates
  never needs a region; one that does cannot silently proceed without it.
- **Exhaustion is a failure, not a wait.** A request that does not fit returns
  `NULL`, with the same consequence.
- Calling `nyrt_heap_init` again replaces the region and discards what was
  handed out of the old one.
- **There is no locking.** A region is a single bump pointer, so two
  concurrent allocations race. Nothing freestanding is emitted with threads,
  so this is a limit of the shipped runtime rather than of the contract: a
  host that needs concurrent allocation implements `nyrt_alloc` itself.

Failures are reported through a weak hook the host may define:

```c
void __ny_host_fail(const char *message);
```

Linking the archive alone produces a complete program: the default is weak, and
`nyrt_fail` stops the caller when no host reporter exists or when one returns
anyway. A host that wants to print, log or panic defines this symbol.

## A minimal host

```c
#include <stdint.h>

extern long __ny_entry(void);
extern void nyrt_heap_init(void *base, uint64_t size);

static unsigned char arena[64 * 1024];   // the vessel's memory, the host's choice

__attribute__((noreturn)) void _start(void) {
  nyrt_heap_init(arena, sizeof arena);
  const long code = __ny_entry();
  my_exit(code);
}
```

Link with no C library at all:

```sh
cc -nostdlib -static -o vessel host.c program.o libnilruntime_freestanding.a
```

If either side still needed libc, this link fails rather than quietly
succeeding against the build host's C library.

## What is checked, and where

Two registered tests pin this contract; they are the authority if this page
and the toolchain disagree.

- `nil.native-symbol-surface` (`tests/tools/native_symbol_surface.py`) reads
  the ELF symbol table directly and pins the import surface: that a
  non-allocating object imports only the five hooks, that an allocating
  freestanding one adds exactly the three allocation hooks and no libc name,
  that a hosted object is entered through `main` and a freestanding one
  through `__ny_entry` with no `main`, and that `noredzone` reaches every
  generated definition when it is requested.
- `nil.freestanding-runtime`
  (`tests/tools/freestanding_runtime_contract.py`) links object and archive
  with `-nostdlib` and its own `_start` and runs the result, requiring the
  exit status to be what Ny returned. For allocation it installs a region,
  then reads the concatenated bytes back out of that region afterwards, and
  separately requires that the same program with no region installed stops
  through `nyrt_fail` with a message instead of continuing.

## What this does not provide

- **No qualification.** See the [v1 release profile](../release/v1-profile.md).
  Capability and release qualification are separate decisions.
- **No evidence that any particular kernel boots a Ny vessel.** Everything
  above is checked on a hosted Linux build host by inspecting and running what
  the toolchain produces. Whether a vessel runs under a given operating system
  is testable only in that system's own repository.
- **No system-call bindings.** A consuming operating system's own ABI —
  its call structures, its trap or interrupt gate, its rights model — belongs
  to that system and stays in its repository, particularly while it is
  unstable. What NIL supplies is the capability to express it: freestanding
  objects, exact record layout through
  [owned scalar buffers](owned-buffers.md) and the
  [native register assembly](inline-assembly.md) profile, and no imports a
  consumer did not ask for.
- **No threads, TLS, unwinding or hosted I/O.** None of these are emitted for
  a freestanding target, and none are supplied by the shipped runtime.
