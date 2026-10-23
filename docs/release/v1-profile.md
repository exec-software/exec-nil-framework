# NIL Framework v1 release profile

The current product identity is **1.0.0-rc.1**. `VERSION` is the authority for
the SDK tools and editor extension. This document defines the bounded first
release; it does not promote every design document to an implemented promise.
No stable v1 tag or publication is implied by building a candidate.

## Host and dependencies

The reviewed binary release profile targets **Ubuntu 24.04 Linux x86_64**,
using LLVM 18.1.3 and GCC 13.3.0. This describes the earlier RC qualification,
not every development build. The current development checkout has been built
with LLVM 21.1.8 and GCC 15.2.0; that toolchain is not covered by the pinned
[release dependency policy](../../release/dependency-policy.json). Before
shipping new binaries, either rebuild and qualify the exact release commit
with the reviewed toolchain or review its replacement, notices, dependencies,
and minimum host requirements together. Windows, macOS, other architectures,
bare-metal or freestanding targets, and older Linux distributions are not
qualified release targets.

The reviewed RC binaries required GLIBC symbols through 2.38, GLIBCXX through
3.4.32, and CXXABI through 1.3.13. These remain policy ceilings, not measured
requirements for the current development SDK or proof of compatibility with
every distribution supplying them. The release
assembler records each executable's direct dependencies and rejects a changed
dependency set or requirements exceeding the reviewed policy. Host transitive
dependencies still need distribution-level validation.

Required host runtime packages include `libc6`, `libstdc++6`, `libgcc-s1`,
`zlib1g`, `libzstd1`, `libtinfo6`, and `libx11-6` (for experimental `nilagent`).
LLVM is linked into the tools; consumers do not need an LLVM installation.
Native AOT compilation and `nillinker` **do require `/usr/bin/c++`**, a compatible
C++ development toolchain, linker, and CRT/development libraries; on the target
distribution these are provided by `build-essential`. The compiler driver is
selected at SDK build time, not by a consumer `CXX` environment override.
The matching `nillib/native/libnil.a` and `libnil.so` must remain with the SDK.

The database provider loads host SQLite and PostgreSQL client libraries at
runtime. They are optional dependencies and their absence is reported by the
capability API. The current development tests cover SQLite and an isolated
PostgreSQL server; a binary release still needs its client-library and target
host combination qualified. See the [database contract](../standard-library/database.md).

The VSIX requires VS Code 1.95 or later and a trusted local workspace for tool
execution. It supports Ny and Matrix syntax, document-local completion,
compiler-backed checking of saved and unsaved buffers, and guarded formatting
fixes. Native debugging, project-wide completion/type inference, and
remote/virtual workspace integration are not claimed. The
[analysis API](../analysis-api.md) documents declaration reflection, diagnostic
locations, and the limits of custom analyzer integration.

## Supported language and tools

The positive route catalog exercises scalar arithmetic and widths, scalar
arrays, aggregate references, globals, managed lifetimes, concrete and
polymorphic classes, synchronous typed faults, local owned buffers, isolated
parallel map, and sleep. The manuals define their precise limits:

- [Types and widths](../language-reference/types.md),
  [classes](../language-reference/classes.md), and
  [fault handling](../language-reference/faults.md).
- [Owned buffers](../advanced/owned-buffers.md): local `i32`, `i64`, and `u8`
  owners, checked moves, lexical loans, and scope cleanup; not borrowed
  parameters, owning aggregate fields, or custom destructors.
- [Structured concurrency](../advanced/structured-concurrency.md): bounded
  isolated scalar workers, copied inputs, ordered results and mandatory join;
  not shared-memory tasks, async I/O, cancellation, or nested parallelism.
- [Inline assembly](../advanced/inline-assembly.md): bounded x86_64 integer
  register operations on native routes; rejected by the interpreter. Pointer
  operands and SIMD remain future work.
- [Executable context](../advanced/executable-context.md): opt-in inspection
  without executing NIL/native artifacts; embedding context can disclose it.
- [Debugger/tool reference](../tools/index.md): source-level NIL DAP/F5 and
  machine-readable inspection. Native DWARF/attach and worker-thread stepping
  are not provided.

Source `println` has exactly one argument on every route, including unchecked
frontend mode. Use `println("")` for a blank line.

The ten SDK commands are `nilcompiler`, `nilinterpreter`, `nilassembler`,
`nillinker`, `nildebugger`, `nillsp`, `nilbuild`, `nilschema`, `nilagent`, and `aperture`.
`nilschema` and `nilagent` are experimental utilities, not v1 language guarantees.
Aperture is a separately versioned analysis/debugging preview; packaging it does
not extend the v1 language or debugger guarantees. The LSP has a deliberately
limited capability set. `nilbuild` supports the [local-project profile](../tools/projects.md),
with process execution qualified on Linux. `nypm` and `nilaix` are not part of this release.

Sixteen logical standard-library modules are strictly compiled and packaged,
including `prelude`. Public imports use `nil.std.*`, with `ny.lang.*` retained
as aliases. Both frontends share the flat module-family layout. Runtime support
is API-specific, as described in the [library status](../standard-library/index.md).
`nil.std.reflection` provides declaration/annotation snapshots, and `nil.std.db`
provides bounded SQLite/PostgreSQL text/NULL operations through shared VM/native
providers. Semantic reference graphs, automatically hosted analyzer plugins,
typed/binary database values, network transports, cryptography, compression,
and general stream handles remain outside these implemented slices.
Quarantined modules are not promoted by their presence in a design or by a
module compiling successfully.

## Install, use, and remove

Verify the release's `SHA256SUMS` against independently obtained manifest and
checksum-file digests before extracting a downloaded artifact. Checksums alone
do not authenticate a publisher. Extract the SDK tarball to a new project-local
or user-owned directory. It contains commands at its root, `nillib` binaries,
`nilstd` sources, and compiled `docs`; do not flatten those directories or mix
generations. Do not run directly from the writable build-server share.

Set `ny.sdkPath` to that SDK root in VS Code and install the accompanying VSIX
using **Extensions: Install from VSIX**. CLI callers can invoke the absolute
tool paths or add the SDK root to their own `PATH`. Use `--version` to verify
that all tools identify the same product version. To remove a project-local
installation, remove its PATH/editor references and move that exact SDK
directory to trash. No system-wide installer or service is required.

Current NIL readers retain explicitly tested legacy formats. NIL library
interfaces are regenerated by the compiler when imported; they are not a
native separate-compilation cache. Product version, NIL format version, and
native runtime ABI version are distinct. Do not substitute a runtime archive
from another SDK. A release version does not promise forward-readable NIL or
an unrestricted foreign ABI.

## Qualification and publication

A releasable artifact must come from a clean, identified commit, pass the
complete LLVM-enabled Release CTest suite without skips, seal and validate its
SDK, pass installed consumer routes, and pass an independent reference and
native-route corpus. Ownership, concurrency, debugger, sanitizer, and
LLVM-disabled evidence must be recorded with their actual tested scope.
Archive assembly is an additional gate, not a replacement for these tests.

The local assembler requires dependency notices and a reviewed dependency
policy, produces a normalized SDK archive and exact asset/checksum manifest,
and can include the matching VSIX. Normalized archiving is not a claim of
bit-for-bit reproducible compiler builds. The SDK manifest retains technical
qualification; the outer release manifest adds product/artifact identity.

The [assembler](../../release/README.md) verifies the exact release assets and
independently retained hash anchors, and refuses to overwrite an existing
release directory. It prints verification commands but never publishes
automatically. `latest` means newest publication and may be an RC; publication
and a stable `v1.0.0` tag remain separate, deliberate release actions.

See the [findings log](../../matrix/docs/next-version-findings.md) for open
correctness issues, including fully qualified enum patterns (F25) and Ny local
reads before definite assignment (F21), alongside native array and diagnostic
limitations. Resolve or explicitly exclude affected behavior from the stable
profile and qualify the final source revision before selecting a stable tag.
Preparing or publishing the source repository does not satisfy these binary
release gates.
