# Anomalous ABI: freestanding native targets

Working log for making NIL emit code that an Anomalous vessel can consume.
Anomalous is a separate project at `exec-anomalous`, an experimental operating
system in Rust that has adopted Ny as its implementation language (its
decision record 0016). It will eventually support several major ABIs and also
ships its own; its own is the one targeted here.

## What the Anomalous ABI is

Two layers, neither of them a calling convention in the usual sense.

**Wire types** live in `crates/anomalous-abi`, a `no_std`, allocation-free
crate holding data layout only. `rite_call` is `#[repr(C)]`: `header_size`,
`operation`, `payload_kind`, `reserved`, `rights_present: u64`,
`payload_size`, `payload_limit`, `trace_id: id_128`, with payload bytes
following. `rite_status` is a `u16` with `ok`, `would_block`, `denied`,
`bad_operation`, `bad_payload`, `limit_exceeded`, `interrupted`, `faulted`.
The crate states the ABI is not stable while Anomalous is pre-alpha.

**The rite gate** is the kernel/user boundary (their decision record
0019). A vessel
writes the call into a fixed mailbox, zeroes scratch registers, and issues
`int $0xf3`; the scheduler gate is `0xf2`. The reply is read back from the
mailbox: forty bytes of `header_size`, `status`, `sequence`, `trace_id`. The
first operations are `console_write`, `input_read`, `task_exit`, `task_yield`
and `display_update`, each gated by one right bit, failing closed.

The useful consequence: this ABI is struct layout plus one interrupt. NIL
needs no new calling convention for it. What it needed was to stop dragging a
hosted runtime along.

## Why the work was NIL-side

Anomalous decision record 0049 qualifies a bounded freestanding Ny object
and records
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
  different questions, and that record describes a working configuration
  whose userspace red zone is fine because interrupt entry switches stacks.
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

## Allocation: the host owns the memory

Decided with the maintainer on 2026-11-09, choosing between a bump allocator
over a host-installed region, imported `nyrt_alloc`/`nyrt_free` hooks the host
must implement, and a static arena inside the runtime. The region won.

What settled it was the import surface. Imported allocation hooks would have
taken the boundary a freestanding consumer must implement from five symbols to
seven, and the whole point of this work was to shrink what they implement, not
move it. A static arena inside the runtime keeps the surface at five but makes
the size of a vessel's memory a NIL decision, charged to every consumer in
BSS. A region the host installs keeps both properties: the host implements
nothing, and the memory is still theirs.

So `nilruntime_freestanding` gained `nyrt_heap_init(base, size)`, exported
rather than imported, and `nyrt_alloc` bumps a 16-byte-aligned, zeroed pointer
through that region. `nyrt_heap_used()` and `nyrt_heap_size()` are there for a
host that wants to see whether a vessel allocated at all. There is no free: a
bump region is released as a whole, and `nyrt_context_end` returns it to
empty, which is sound because everything allocated inside it came from
generated code whose lifetime ended with the context. Nothing in the runtime
declares a heap of its own.

The emission side changed with it. `__ny_string_concat` called `strlen`,
`memcpy` and `calloc`; under `--freestanding` it now calls `nyrt_str_length`,
`nyrt_mem_copy` and `nyrt_alloc` instead. The helper's shape is otherwise
identical, including the null check -- an allocator with no region returns
null exactly as a hosted one does under pressure, and the existing failure
path reports it through `nyrt_fail` with a message naming the operation. So
"no region installed" needed no new failure semantics; it reuses the one that
was already pinned.

The two new names are also defined in the hosted runtime. Without that, an
object emitted with `--freestanding` could only ever link against the
freestanding archive, and which runtime to link would have stopped being the
host's choice.

Measured, not asserted: a freestanding concatenating object imports
`nyrt_abi_version`, `nyrt_alloc`, `nyrt_context_begin`, `nyrt_context_end`,
`nyrt_fail`, `nyrt_mem_copy`, `nyrt_profile_site`, `nyrt_str_length` -- eight
names, no libc symbol at all. The hosted object for the same program still
imports `calloc`, `memcpy` and `strlen`, unchanged. The archive still imports
nothing but the weak `__ny_host_fail`. Checked with `nm -u` directly as well
as through the test.

`nil.native-symbol-surface` is now 12 checks: the three added ones pin that a
freestanding allocating object reaches memory through the runtime hooks, that
no libc name survives in it, and that allocation widens the surface by exactly
those three names and nothing else.

`nil.freestanding-runtime` is now 8 checks, and the two added cases run rather
than inspect. The first installs a 4 KiB array in the driver, runs a program
that concatenates `"left"` and `"right"`, and afterwards searches the driver's
own array for `leftright` -- so what is proved is that the bytes landed in
memory the host owns, which no symbol inspection could show. It also requires
`nyrt_heap_used()` to be zero on return, pinning the reset at context end. The
second links the same program with no region installed and requires it to stop
through `nyrt_fail` with a non-null message, because an allocator that cannot
say no is worse than none. Each case is the other's control: the success path
proves allocation works, the starved path proves it was really the allocator
being called.

## The ABI document

`docs/advanced/freestanding-targets.md`, linked from `docs/README.md` and
`docs/advanced/index.md`. It states the emission flags, the `__ny_entry`
contract and the exact order of the entry sequence, the five always-required
hooks with C signatures, the three allocation hooks and the fact that they
appear only if the program allocates, what `nilruntime_freestanding` supplies,
the heap division and its four consequences, the weak `__ny_host_fail`, a
minimal host, and which test pins which claim. It names the tests as the
authority if the page and the toolchain disagree.

It also states what the contract does not provide, which is the part worth
keeping honest: no release qualification, no evidence that any kernel boots a
Ny vessel, no system-call bindings, and no threads, TLS, unwinding or hosted
I/O.

Writing it turned up one thing worth noting: NIL's repository policy forbids
the retired `ADR nnnn` spelling repository-wide, and the 2026-11-09 notebook
entry cited Anomalous's decision records in exactly that form, so
`ny.repository-policy` had been failing since `00eced9`. The citations are
reworded. An exemption would have been the wrong fix -- the rule exists to
stop NIL's own retired spelling creeping back, and a hole in it would hide
real drift.

## Open, in the order they probably matter

1. **A vessel that allocates more than one string.** The bump region is never
   reclaimed inside a context, so a vessel that concatenates in a loop
   exhausts it and stops. That is the correct behaviour for the current
   contract and the wrong behaviour for a long-running vessel. Whether the
   answer is reclamation, a per-operation scope, or a region the host rotates
   is a question that needs a real vessel to ask it; guessing now would build
   the wrong thing precisely.
2. **Rite bindings stay in Anomalous.** Agreed with the maintainer: their ABI
   is theirs and is explicitly unstable, so it should not enter `nilstd`. NIL
   supplies the capability -- freestanding objects, exact record layout,
   inline assembly, no unconditional imports.
3. `docs/release/v1-profile.md` still lists freestanding targets as not
   qualified for release. That remains true and deliberate; qualification is a
   separate decision from capability.
4. **Still nothing here proves a vessel boots or issues a rite.** That needs
   the `int $0xf3` path and their repository. What is now removed is the
   reason they needed a reverse-engineered shim, trap stubs for libc imports
   they never wanted, and an allocator they would have had to implement.

## Branch state

All of the above is on `anomalous-abi-symbol-surface`, unmerged. It
also carries `72c9547`, the macOS desktop acceptance record, because the
branch was cut from `macos-ui-acceptance-record` before that merged. Merging
this branch therefore brings the macOS documentation with it and makes
`macos-ui-acceptance-record` redundant.
