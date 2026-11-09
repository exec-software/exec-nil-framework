# Anomalous ABI: freestanding native targets

Working log for making NIL emit code that an Anomalous vessel can consume.
Anomalous is a separate project at `exec-anomalous`, an experimental operating
system in Rust that has adopted Ny as its implementation language (its ADR
0016). It will eventually support several major ABIs and also ships its own;
its own is the one targeted here.

## What the Anomalous ABI is

Two layers, neither of them a calling convention in the usual sense.

**Wire types** live in `crates/anomalous-abi`, a `no_std`, allocation-free
crate holding data layout only. `rite_call` is `#[repr(C)]`: `header_size`,
`operation`, `payload_kind`, `reserved`, `rights_present: u64`,
`payload_size`, `payload_limit`, `trace_id: id_128`, with payload bytes
following. `rite_status` is a `u16` with `ok`, `would_block`, `denied`,
`bad_operation`, `bad_payload`, `limit_exceeded`, `interrupted`, `faulted`.
The crate states the ABI is not stable while Anomalous is pre-alpha.

**The rite gate** is the kernel/user boundary (their ADR 0019). A vessel
writes the call into a fixed mailbox, zeroes scratch registers, and issues
`int $0xf3`; the scheduler gate is `0xf2`. The reply is read back from the
mailbox: forty bytes of `header_size`, `status`, `sequence`, `trace_id`. The
first operations are `console_write`, `input_read`, `task_exit`, `task_yield`
and `display_update`, each gated by one right bit, failing closed.

The useful consequence: this ABI is struct layout plus one interrupt. NIL
needs no new calling convention for it. What it needed was to stop dragging a
hosted runtime along.

## Why the work was NIL-side

Anomalous ADR 0049 qualifies a bounded freestanding Ny object and records
exactly what that cost them. The SDK emitted Linux host-targeted ELF plus a
generated C `main(int, char **)` wrapper, so a "separate narrow assembly shim
calls the generated C wrapper with zero arguments and implements only its
observed ABI-version, context-begin, context-end and failure hooks". The shape
was observed because nothing published one. The compiler's "unconditional
unused string helper imports `strlen`, `calloc`, and `memcpy`", which they
satisfied with fatal trap stubs that fail the proof if reached. The ordinary
runtime assumed hosted allocation, OS services and TLS, which "would silently
exceed the qualified kernel profile". And "Ny has no red-zone-disabling CLI
option".

Every one of those is a NIL emission problem, and every one is testable on
Linux by inspecting what the toolchain produces. None of it needed a Mac, a
VM, or CI.

## What changed

- **The string helper is built on first use.** `__ny_string_concat` was
  created in every module while its only caller looked it up on demand, so
  `strlen`, `memcpy` and `calloc` were undefined in the object of a program
  that never concatenated anything. An object for such a program now imports
  exactly the five native runtime hooks and no libc at all.
- **`nilassembler --freestanding`** emits the same entry sequence -- runtime
  ABI check, context begin, global initialisation, user main, context end --
  as `__ny_entry()` taking no arguments, defining no `main`. The argc/argv
  derivation is skipped rather than fed zeroes, so no argument-vector code is
  emitted at all.
- **`nilassembler --no-red-zone`** marks every generated definition
  `noredzone`. Deliberately independent of `--freestanding`: the two answer
  different questions, and ADR 0049 describes a working configuration whose
  userspace red zone is fine because interrupt entry switches stacks.
- **`nilassembler --emit-llvm <path>`** writes the generated IR. The red-zone
  attribute does not survive into an object, so without this the guarantee
  could only be asserted, not checked.
- **`nilruntime_freestanding`** implements the five hooks with no libc,
  kernel or allocator. The archive imports nothing but a weak
  `__ny_host_fail`, which a host defines to observe a failure and which
  defaults to stopping the caller. It deliberately does not allocate, format,
  read a clock or touch a device.

## What is actually proved

`nil.native-symbol-surface` reads the ELF symbol table directly, so it needs
no binutils. It pins that a plain object imports only
`nyrt_abi_version`, `nyrt_context_begin`, `nyrt_context_end`, `nyrt_fail` and
`nyrt_profile_site`; that a concatenating program still pulls the helper's
dependencies back, so the first assertion cannot pass by the helper having
been dropped entirely; that a hosted object is entered through `main` and a
freestanding one through `__ny_entry` with no `main`; and that `noredzone`
reaches every definition, checked through the attribute groups the
definitions reference rather than by grepping for the word.

`nil.freestanding-runtime` links the emitted object and the archive with
`-nostdlib` and its own `_start`, so a hosted toolchain cannot satisfy a
symbol by accident -- if either side still needed libc the link fails rather
than succeeding quietly on the build host -- then runs the program and
requires the exit status to be the value Ny returned. It also links a program
that replaces the weak reporter, since a hook that cannot be overridden is no
use to a host.

Linux stands at 574 registered, 0 failed, 3 environment skips.

## What is not proved

Nothing here shows that a vessel boots under the Anomalous kernel or issues a
rite. That needs the `int $0xf3` path and their repository. What these changes
remove is the reason they needed a reverse-engineered shim and trap stubs.

## Open, in the order they probably matter

1. **Allocation.** The moment a vessel concatenates a string it needs
   `calloc`, and the freestanding runtime deliberately has none. A bump
   allocator in the runtime, or a host-delegated hook, is a decision about who
   owns vessel memory rather than a gap to fill quietly. The maintainer
   expects later features to need the shipped runtime, so this is the next
   real question.
2. **A written ABI document.** The contract is currently pinned by tests and
   described here. Anomalous should be able to read it rather than infer it
   from our test file: the five hooks and their signatures, `__ny_entry`, the
   flags, and what a host must supply.
3. **Rite bindings stay in Anomalous.** Agreed with the maintainer: their ABI
   is theirs and is explicitly unstable, so it should not enter `nilstd`. NIL
   supplies the capability -- freestanding objects, exact record layout,
   inline assembly, no unconditional imports.
4. `docs/release/v1-profile.md` still lists freestanding targets as not
   qualified for release. That remains true and deliberate; qualification is a
   separate decision from capability.

## Branch state

All of the above is on `anomalous-abi-symbol-surface`, pushed, unmerged. It
also carries `72c9547`, the macOS desktop acceptance record, because the
branch was cut from `macos-ui-acceptance-record` before that merged. Merging
this branch therefore brings the macOS documentation with it and makes
`macos-ui-acceptance-record` redundant.
