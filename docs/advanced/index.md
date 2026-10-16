# Advanced implementation topics

This page describes the current architecture and where experimental work lives.
Ny's long-term systems-language design includes stronger safety, concurrency,
interop, and compile-time facilities, but those goals must not be read as
implemented guarantees.

## Compilation pipeline

`nilcompiler` owns the lexer, parser, AST, type checker, module/import handling,
and code generators. Its primary development path emits Ny Intermediate
Language (NIL), which `nilinterpreter` and `nildebugger` consume.

```text
.ny source
   -> parser and AST
   -> type checker
   -> NIL generator
   -> .nil
   -> nilinterpreter or nildebugger
```

Textual and binary NIL loaders share implementation code under
`nilcompiler/src`. Readers still accept the older `MIL` binary identity for
compatibility, while newly emitted files and public APIs use NIL.

## Optional native backend

An LLVM native path is under development:

- `nilcompiler --native` uses LLVM when the SDK was configured with
  `NIL_ENABLE_LLVM=ON` and LLVM was found. `--jit` selects LLVM JIT execution.

Backend feature coverage is not identical. A program succeeding in the interpreter does
not yet guarantee that it succeeds through LLVM, and the converse can also
expose different runtime behavior.

The first-class `nilassembler` and `nillinker` commands expose that LLVM path as
separately testable stages. The assembler strictly loads a compiler-emitted
executable NIL module and emits a genuine host relocatable object. The linker
validates host objects and the runtime from bounded immutable snapshots, then
invokes the configured compiler driver with an argument vector and bounded
process-lifetime cleanup to produce a native executable linked against the
matching SDK runtime archive.
They deliberately do not define another Ny-specific object or executable
container. [Executable binary NIL](nil-format.md) retains schemas and
function annotations in v3, with class ancestry and method metadata added in
class-bearing v4 artifacts. Legacy v1 inputs still fail
explicitly when their omitted metadata is required.

The first [native register assembly](inline-assembly.md) profile has explicit
x86_64 targeting, checked `i64` operands, preserved NIL instructions, and a
bounded straight-line instruction set. It is native-only; interpreter routes
reject it rather than omitting the code. General-purpose inline assembly is
still outside this profile.

## Runtime memory modes

`nilinterpreter` currently exposes `--memory-mode=safe` (also `gc`) and
`--memory-mode=unsafe` (also `manual`). Manual/unsafe is the current default.
The LLVM backend also has a manual mode selected by `nilcompiler --unmanaged`.

These runtime/code-generation switches are separate from explicit
[owned scalar buffers](owned-buffers.md): checked local affine moves, lexical
shared/exclusive borrowing and deterministic scope/fault cleanup. This initial
contract is not general ownership of arbitrary aggregates or a complete
Rust-style lifetime system.

Local declarations now have distinct NIL storage identities. Nested locals,
loop bindings, and match payloads may shadow enclosing variables without
changing their values or mutability; disjoint scopes may reuse a name with
different types. Initializers and loop iterables resolve in the enclosing
scope before their new binding is introduced. Duplicate declarations in the
same scope and assignments to immutable bindings are still rejected.

With `--debug-info`, per-instruction source paths and lexical names map these
storage identities back to visible variables; scratch slots remain hidden from
source locals. Without this opt-in metadata, artifact-level inspection still
uses disambiguated `$ny.local.*` and `$ny.temp.*` names.

## Structured parallel execution

[Parallel map](structured-concurrency.md) runs isolated scalar workers on
bounded host threads, copies inputs, preserves output order and joins before
completion or failure. It has VM and LLVM implementations, not a sleep-based
simulation. Shared-memory tasks, captures, channels, asynchronous I/O and
nested parallelism remain outside this contract.

## Interoperability

The compiler and NIL runtime contain interop machinery, and
`nilstd/native` preserves C/C++ shims for selected modules. Interop is
currently component- and platform-specific: there is no frozen Ny ABI, complete
header-generation workflow, or guarantee that every `@interop`/native wrapper
works through every backend.

Native dependencies should be connected through explicit CMake targets and
tested with the backend that will ship them. `nillinker` currently accepts host
objects and the version-matched Ny runtime, but it does not yet implement
library search flags or a frozen general-purpose Ny ABI.

## Debug and automation surfaces

- NIL includes source-line/debug metadata used by `nildebugger`.
- `--debug-info` adds source maps and lexical locals. `nildebugger --dap` and
  VS Code F5 provide source breakpoints, stepping, frames, streamed output and
  cooperative pause through the real interpreter. Native DWARF/attach is
  separate and unavailable.
- `nildebugger --json` exposes debugger operations as strict JSON Lines for tools
  and agents. Its stack, local, and global views come from the interpreter's
  actual runtime state, and program stdout is kept in framed output events.
- Function-level `@ai_context` annotations are retained by the parser; debug
  and test code generation contains metadata-registration hooks.
- [Executable context](executable-context.md) provides read-only JSON
  inspection and opt-in native `.ny.context` embedding on Linux x86_64.
- A richer context extractor exists in `nilcompiler/src`, but is not a
  command-line tool.
- Windows `nilinterpreter --ai` contains a partial, opt-in live-control server. Unix
  transport and interpreter introspection are unfinished.
- `nilaix` contains explicit `.nycrash` writers and application integration
  experiments.

See [NIL-0006](../design/NIL-0006-ai-module.md) for the exact implemented control
boundary.

## Experimental language areas

The repository contains parser, compiler, or library work for generics, traits,
structured types, pointers, concurrency, testing constructs, engine/UI code,
and other systems-language features. Support can be partial and backend
dependent. Validate each feature with a focused source fixture and current
compiler diagnostics before depending on it.

The [language reference](../language-reference/lexical-structure.md) records the
language direction; [tests/README.md](../../tests/README.md) explains which
checked-in files are automated tests versus development fixtures.
