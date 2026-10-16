# Authority, sandboxing, and the kernel boundary

Matrix's reason to exist beyond syntax: it is intended to become the default
both-AI-and-human language for embedded and OS-level settings, with the NIL runtime
integrated *inside* the kernel rather than running on top of it. The purpose is
to sandbox agents, humans, and the kernel itself — and to sandbox the language
at compile time against a kernel-enforced authority set, with the chain of
trust starting at the boot loader.

This document records the design rule that follows from that, and what it
costs. Nothing here is implemented. The base surface in `samples/` does not
depend on it, and the grammar reserves the syntax so adding it later is not a
breaking change.

## The rule: full power, zero ambient authority

Matrix keeps every capability a systems language needs — raw pointers, pointer
arithmetic, inline assembly, foreign calls, syscalls, device memory. **Nothing
is removed to make the language safe.** A systems language that cannot touch
hardware is not a systems language, and a sandbox built by amputation is a
sandbox nobody will use, which means it protects nothing.

What goes away is *ambient* authority: the property that importing a module, or
simply being code, grants the power to act. In Matrix, authority is

- **declared** — it appears in the source and in the artifact,
- **named** — it is a specific authority, not a privilege level,
- **verified** — computed from the artifact, not taken on the compiler's word,
- **attenuable** — a component can be handed strictly less than it holds,
- **refusable** — the kernel decides before the first instruction executes.

The difference is not "you may not do X". It is "you must say that you do X,
and something other than you decides whether you may."

## Why the artifact, not the source, is the thing checked

A kernel sees NIL. It does not see Matrix, and it has no reason to trust
whatever produced the NIL — not `matrixc`, not `nilcompiler`, not a hand editor.
Source-level checking is a developer convenience. Artifact-level verification
is the security boundary.

This is not a new mechanism to invent. **Ny already ships it, in narrow form.**
`parallel_map` worker call graphs are verified against an explicit allowlist of
pure opcodes across the complete transitive call graph, including branches that
never run. From `nilcompiler/src/concurrency_contract.cpp`:

```cpp
// An allowlist is intentional: adding a language opcode cannot accidentally
// grant a worker access to process state, handles, pointers, FFI or I/O.
const std::unordered_set<std::string> pure_operations = { ... };
```

and the published contract states the property that matters:

> These restrictions are checked on NIL as well as source, so hand-written NIL
> and `--no-typecheck` cannot grant a worker additional effects.

That is a working, tested, artifact-level effect verifier with a default-deny
allowlist and a fail-closed unknown-opcode path. The synchronous fault protocol
has a comparable NIL-level verifier. The capability system should **generalise
this code**, not replace it: the same transitive-closure walk, the same
default-deny posture, with a set of named authorities instead of one hardcoded
"pure" set.

## Shape of the design

Three pieces, in dependency order.

**1. An authority vocabulary.** A closed, kernel-defined set of named
authorities, each corresponding to a class of NIL operation or intrinsic
provider: `heap`, `raw-memory`, `machine-code`, `console`, `file-read`,
`file-write`, `clock`, `threads`, `foreign`, `port-io`, `interrupts`. Closed,
because an open vocabulary is not verifiable; the kernel must know what a name
means.

**2. A declaration in the surface, and a clause reserved for it now.**

```text
area driver.nvme requires raw-memory, port-io, interrupts
area parser.json                       !!! declares nothing, so may do nothing
```

The compiler computes the authority a module *actually* uses, by the transitive
walk above. Under-declaring is a compile error. Over-declaring is a lint, and
deliberately not an error — a module may want a stable declared surface across
versions.

**3. A manifest section in NIL, and a decision record for it.** The authority
set must be a first-class section of the artifact, so the kernel can read it
without executing or trusting anything. Per
[NIL-0003](../../notebook/decisions/NIL-0003-serialized-nil-is-a-toolchain-boundary.md),
a format change of this kind wants a written compatibility table, old-version
fixtures, round-trip tests, and a decision record of its own before encoding
anything in reserved sections. Section 4 is currently `reserved u32 0` with
nonzero records rejected — which is the correct fail-closed behaviour and
exactly why it cannot simply be repurposed.

The kernel's runtime check then reduces to: `declared ⊆ granted`, evaluated
once, before the first instruction. Everything expensive happens at compile and
load time.

## What this is not

It is not a privilege level, a ring, or a user ID — those are coarse and
ambient, which is the thing being removed. It is not a runtime permission
prompt. It is not a guarantee that verified code is *correct*; an authority
says what a module may reach, not that it uses it well. And an authority
manifest is **not** a substitute for the boot chain: a manifest the kernel
reads is only as trustworthy as the kernel that reads it, which is why the
stated intent starts at the boot loader rather than at the language.

Nor does it make the current `nilinterpreter` a kernel-resident VM. The existing
VM is ~6,200 lines of host C++ with standard-library containers and host
allocation. A kernel-resident NIL executor is a separate, considerably harder
artifact with its own bounded-memory, deterministic-failure and auditability
requirements. The language work does not depend on it, but no claim about
kernel integration should lean on the interpreter as it stands.

## Why this helps the token-cost goal too

A declared authority set is a few tokens at the top of an area that tell an
agent, a reviewer, and the kernel exactly what the code may reach. That is
compression of intent: it replaces reading a body to find out whether it
touches the disk. It also narrows generation — an agent writing in an area that
declares nothing cannot usefully emit a syscall, so the tokens spent attempting
one, and the review round trip that rejects it, are never spent.

## Immediate consequences for the base implementation

- `requires` is reserved in the grammar now, so adding it is additive.
- Inline assembly **stays** in the surface (`samples/13`, `samples/15`). The
  VM-first focus already means interpreted routes reject `INLINE_ASM`, so
  nothing in the VM path executes machine code today; that is a route fact, not
  the security design.
- `note intrinsic` stays writable, but it is the authority surface in
  miniature: naming a provider is requesting a capability. Whether an ordinary
  area may declare intrinsics at all is the first real policy question to
  settle, and it is settled in the authority design, not by restricting syntax.
- Capabilities belong in the **shared core**, not in a Matrix-only AST node.
  The artifact carries them, the verifier is shared, and Ny therefore gets them
  too. This preserves the one invariant that keeps the two languages from
  drifting (see [`../README.md`](../README.md)); a Matrix-only semantic feature
  would break it.
