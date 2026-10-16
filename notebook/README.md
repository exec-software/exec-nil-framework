# Ny engineering notebook

This directory is Ny's project-local engineering notebook. It records what the
implementation actually does, why architectural decisions were made, and which
experiments are safe to build next. It is intentionally versioned with the
compiler so notes cannot silently drift away from the code they describe.

Ny is designed for human and AI collaborators. Notebook entries should
therefore be useful to both: concise enough to scan, precise enough to automate,
and explicit about evidence, uncertainty, and implementation status.

## Contents

- [`2026-09-09-recovery-baseline.md`](2026-09-09-recovery-baseline.md) — the
  first verified snapshot after the repository cleanup.
- [`2026-09-13-toolchain-debugger-milestone.md`](2026-09-13-toolchain-debugger-milestone.md)
  — real serialized-NIL/object/link stages, VM-owned debugging, the hardened
  debugger artifact boundary, and the SDK integrity contract that keeps those
  claims reproducible after installation.
- [`decisions/NIL-0001-production-apps-are-conformance.md`](decisions/NIL-0001-production-apps-are-conformance.md)
  — why executable applications drive language stabilization.
- [`decisions/NIL-0002-nilinterpreter-is-the-runtime-command.md`](decisions/NIL-0002-nilinterpreter-is-the-runtime-command.md)
  — the canonical name and boundary of Ny's NIL interpreter.
- [`decisions/NIL-0003-serialized-nil-is-a-toolchain-boundary.md`](decisions/NIL-0003-serialized-nil-is-a-toolchain-boundary.md)
  — why assembler/linker stages emit real host artifacts and make serialized
  NIL a tested backend boundary.
- [`decisions/NIL-0004-debugger-state-belongs-to-the-vm.md`](decisions/NIL-0004-debugger-state-belongs-to-the-vm.md)
  — why debugging and AI inspection must view the interpreter's one canonical
  runtime state instead of maintaining shadow frames.
- [`decisions/NIL-0005-generated-nonvoid-fallthrough-is-explicit.md`](decisions/NIL-0005-generated-nonvoid-fallthrough-is-explicit.md)
  — why a defensive return after branching control flow is serialized as an
  explicit fail-closed path instead of an ordinary value-less `RET`.
- [`design/ai-native-runtime.md`](design/ai-native-runtime.md) — candidate
  systems for executable context, structured traces, diagnostics, and replay.
- [`design/executable-context-manifest.md`](design/executable-context-manifest.md)
  — opt-in annotation disclosure, bounded native records, read-only inspection
  and artifact parity contracts.
- [`design/fault-execution-contract.md`](design/fault-execution-contract.md)
  — implemented synchronous fault contract: exact payloads, cleanup, GC roots
  and cross-route tests; linked follow-up records qualification.
- [`design/polymorphic-class-execution.md`](design/polymorphic-class-execution.md)
  — class identity, static views, inherited receiver adaptation and shared
  serialized validation boundaries.
- [`design/standard-library-capability-ledger.md`](design/standard-library-capability-ledger.md)
  — a route-scoped evidence model for standard-library and optional-provider
  support.

## Entry contract

Every substantial entry should include:

1. **Status** — observed, proposed, experimental, implemented, or verified.
2. **Question** — the behavior or design decision under investigation.
3. **Evidence** — source paths, commands, output, or a reproducible test.
4. **Decision** — what Ny will do now, including deliberate non-decisions.
5. **Follow-up** — a small next action with a measurable completion condition.

Use ISO dates for audit and work-log filenames. Architecture decisions belong
in `decisions/` and use the `NIL-`, `NY-`, or `MAT-` identifiers defined in the
[design index](../docs/design/README.md); open-ended ideas belong in `design/`.

## Execution evidence

When a feature is claimed to work, record which route proved it:

```text
Ny source
  -> frontend/type checker
  -> textual NIL -> nilinterpreter
  -> binary NIL  -> nilinterpreter
  -> bundled NIL executable
  -> LLVM JIT
  -> native object/link/run
```

A route that silently ignores an instruction is a failure. A test passes only
when it checks exit status, stdout/stderr, and the intended side effect.
