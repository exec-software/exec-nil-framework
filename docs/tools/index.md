# Tool reference

NIL Framework's tools cover Ny/Matrix source compilation, NIL interpretation, debugging,
basic editor services, and explicit native assembly/link stages. The schema
extractor and agent controller remain previews with unstable interfaces.

Commands below use the installed SDK path from `./scripts/build-sdk.sh`. Add
`sdk` to `PATH`, or prefix each command with that directory. SDK commands
live directly at the SDK root.

## SDK contract

A complete SDK has the following top-level shape:

```text
nilcompiler  nilinterpreter  nilassembler  nillinker
nillsp       nildebugger     nilschema     nilagent
nilbuild     aperture
nillib/      nilstd/         docs/         manifest.json
```

`nilstd` contains the standard-library Ny and Matrix source hierarchy (`.ny`,
`.matrix`, and `.mat`). `nillib` mirrors
that hierarchy with strictly type-checked binary `.nil` artifacts. A compiler
consumer can resolve imports from either tree; SDK validation deliberately
compiles its smoke program with only `nillib` visible. `docs` contains generated
HTML, and `manifest.json` records the build configuration, module inventory,
aliases, and hashes for the SDK files. The SDK builder validates all of these
artifacts before it publishes the generation.

NIL v2 libraries are bootstrap module capsules: they contain compiled NIL
bodies plus the checked Ny or Matrix source interface needed for generic instantiation
and semantic analysis. The current compiler re-checks that embedded interface
and regenerates imported bodies. Direct compiled-body linking is a later format
step, so `nillib` is a source-independent distribution today, not yet a
zero-recompilation module cache.

Earlier sealed `nil.sdk.v1` generations contain the original nine commands;
validation continues to accept that complete inventory. New SDK builds require
Aperture, include its digest in the manifest and build evidence, and check its
help/version identity. Aperture retains its own preview version.

## `aperture`

Aperture records runtime memory activity from Ny and Matrix programs. Use
`aperture memory --output memory.json example.matrix` for the interpreter or
add `--native` for native compilation and execution. Inspect a saved report with
`aperture memory --report memory.json`, optionally adding `--json`.

Reports include allocated, freed, live and peak logical storage, allocation
sites, and GC activity within their stated accounting scope. They do not measure
process RSS or establish memory leaks. Recording currently requires a POSIX
host; saved-report inspection does not execute the target. Heap snapshots,
CPU tracing, code exploration and debugger integration remain planned. See the
[Aperture overview](../../aperture/README.md) for options and limitations.

## `nilbuild`

The [project driver](projects.md) provides `describe`, `check`, `build` and
`run` over a `nil.project.json` file. It shares compiler analysis and project
settings with the VS Code extension. Both VM and native targets are available;
project process execution is currently qualified on Linux.

## `nilcompiler`

`nilcompiler` parses and type-checks Ny and Matrix inputs. It can generate
NIL or use the optional LLVM backend.

For Matrix prompt-style input, `--check-submission <path|->` checks local
statements and a final expression against the initial pure shell profile.
`--check-submission-request <path|->` checks the equivalent structured NILData
request. Both return JSON without execution or retained session state. See
[Matrix submission inspection](matrix-submissions.md).

```text
nilcompiler [options] --output <output> <input.ny> [more-inputs.ny ...]
nilcompiler <input.ny> [more-inputs.ny ...] <output.nil>
```

The explicit `--output` form is recommended because the positional output rule
depends on the selected backend.

### Output paths

| Option | Current behavior |
| --- | --- |
| `-o`, `--out`, `--output <path>` | Set the output path. |
| `--no-bundle`, `--bundle=false` | Leave generated NIL as a separate file. |
| `--bundle`, `--bundle=true` | Append NIL to `nilinterpreter`, found beside `nilcompiler` first and then on `PATH`. This is the default for the NIL path. |
| `--emit-binary-nil` | Emit the binary form of NIL. The old `--emit-binary-mil` spelling remains accepted for compatibility. |
| `--emit-library` | Emit a NIL library capsule (v2, or v4 when carrying classes). Requires `--emit-binary-nil --no-bundle`, cannot be combined with `--native`, and expects all explicit inputs to declare one logical module. |
| `--embed-context` | Opt native file output into retaining annotated function context. Requires `--native`, not JIT. Linux x86_64 only. |
| `--debug-info` | Retain source paths and lexical variable maps in executable NIL for source debugging. Binary output uses v5. Not available with `--native` or `--emit-library`. |

For a deterministic compiler/interpreter workflow, request a separate NIL file:

```bash
nilcompiler --no-stdlib --no-bundle --output hello.nil hello.ny
nilinterpreter hello.nil
```

### Optional LLVM path

| Option | Current behavior |
| --- | --- |
| `--native` | Select the LLVM native backend. Available only in an LLVM-enabled build. |
| `--jit` | Execute using LLVM JIT; meaningful with `--native`. |
| `--unmanaged` | Select manual/unsafe memory mode for LLVM generation instead of its default safe/GC mode. |
| `-l`, `--link <library>` | Add a library for LLVM JIT loading or native linking. Repeat as needed. |

The LLVM backend remains experimental. Configure it with
`-DNIL_ENABLE_LLVM=ON`; LLVM is not required for NIL interpretation. On Linux
and macOS, AOT linking uses CMake's configured C++ compiler driver unless
`-DNIL_NATIVE_LINKER_DRIVER=/path/to/driver` selects another compatible driver.

Safe native mode traces managed allocations at generated function-entry and
control-flow-cycle safepoints, using compiler-published roots. Stale roots can
retain allocations until their function returns. Raw-pointer buffer metadata
still has invocation lifetime, and `--unmanaged` retains manual-memory
semantics. This tracing system is separate from explicit [owned buffers](../advanced/owned-buffers.md), whose checked lexical moves/borrows and deterministic cleanup also operate in unmanaged builds.

### Inputs, library lookup, and diagnostics

| Option | Current behavior |
| --- | --- |
| `--no-stdlib` | Do not try to add first-party library sources automatically. |
| `-I`, `--sdk`, `--sdk-path <path>` | Add a source or SDK search root for imports and library discovery. An SDK root selects its `nilstd` tree. Repeat as needed. |
| `--no-typecheck` | Continue after type-check diagnostics. Intended for compiler development, not normal builds. |
| `-q`, `--quiet` | Print errors only. |
| `-v` | Verbose diagnostics. |
| `-vv`, `-vvv` | Debug diagnostics. Both currently select the same maximum level. |

`nilcompiler` provides shared, checked NILData conversions:

```text
nilcompiler --to-nildata <input.json>
nilcompiler --to-json <input.nildata>
nilcompiler --import-nydata <legacy-library.nydata>
nilcompiler --import-nydata-compiler <legacy-compiler.nydata>
```

They write a complete converted document to standard output, preserving scalar
types and exact number tokens. Malformed input fails without partial output.
Legacy imports are explicit; `--to-nydata` is retired. See the
[NILData format and API](../standard-library/nildata.md) for grammar, bounds,
array semantics and migration rules.

`nilcompiler --inspect-context <artifact>` is a separate, read-only operation
that prints context JSON from executable NIL or a native ELF file containing
`.ny.context`. It never runs the artifact and cannot be mixed with compilation
options. See [executable context](../advanced/executable-context.md) for the
disclosure limits and standalone assembler/linker workflow.

The compiler does **not** currently implement the older documented flags
`--emit-llvm`, `--emit-obj`, `--ast`, `--tokens`, `--target`, `--sysroot`, or
optimization-level switches such as `-O2`.

## `nilinterpreter`

`nilinterpreter` executes textual or binary NIL. It can also run a compiler-produced
bundle when NIL has been appended to the interpreter executable.

```text
nilinterpreter [--memory-mode=safe|unsafe] [--ai] <program.nil> [-- args...]
```

Accepted memory-mode spellings are `safe`/`gc` and `unsafe`/`manual`; the
current default is manual/unsafe. `--no-ai` explicitly disables AI-control mode.

Each interpreted call owns an operand-stack region: its parameter stores and
operations cannot consume or mutate operands suspended in the caller. Calls
check argument availability before builtin output or interop dispatch. This
also applies to the debugger and embedded VM. Typed NIL functions must return
with exactly zero frame-local values for `void`, or one for a non-void return;
falling through or jumping outside that function without `RET` is an error.
These are operand-count checks, not complete runtime return-type validation.

Void calls leave no result, and `POP` always consumes one value (an empty stack
is an error). Expression statements discard real results only. Explicitly
typed void `INTEROP_CALL` instructions discard optional host status values;
process termination is not a normal return. Recompile older development NIL
that contains a redundant `POP` after a void call. No serialized format changed:
the interpreter now retains the return signature already present in both NIL
formats. Historical functions with an omitted return signature and three-operand
interop instructions retain their untyped behavior; they are not the typed
candidate contract.

`SET_INDEX` consumes its array/pointer, index, and value without producing a
result. Raw NIL `NEW_ARRAY` now creates a zero-initialized heap-backed `array<i64>`,
matching native NIL. Embedded legacy by-value `ArrayValue` writes fail explicitly;
hosts must use a heap-array handle for mutation.

`--ai` is an opt-in NIL-0006 prototype. On Windows it starts a per-process named
pipe and dispatches `vm.status`, `vm.pause`, `vm.resume`, and WebView-specific
`vm.send_msg` requests. Stack, local, global, and expression introspection return
placeholders. The Unix socket server is not implemented. See
[NIL-0006](../design/NIL-0006-ai-module.md).

## `nildebugger`

`nildebugger` loads a NIL program into an interactive debugger:

```text
nildebugger [--memory-mode=<mode>] run <program.nil> [-- <args...>]
nildebugger --json [--memory-mode=<mode>] run <program.nil> [-- <args...>]
nildebugger help
```

The text REPL currently supports:

| Area | Commands |
| --- | --- |
| Execution | `run`, `step`, `next`, `out`, `continue` |
| Breakpoints | `break <file>:<line> [if <condition>]`, `delete`, `enable`, `disable`, `cond`, `breakpoints` |
| Inspection | `print <expr>`, `locals [frame]`, `regs`, `backtrace`, `frame <n>` |
| Watches | `watch <expr>`, `unwatch <id>`, `watches` |
| Session | `help`, `quit` |

`--json` changes stdin/stdout to one JSON object per line for IDE and agent
integration. Its command names are `run`, `step`, `next`, `out`, `continue`,
`break`, `delete`, `enable`, `disable`, `cond`, `list_bps`, `eval`, `locals`,
`regs`, `stack`, `watch`, `unwatch`, `watches`, `frame`, and `quit`. This is a
small JSON Lines protocol, not JSON-RPC or the Debug Adapter Protocol. The
separate `--dap` mode described below provides DAP. Requests
are parsed as complete JSON values, and normal Ny stdout is emitted as framed
`output` events so it cannot corrupt the control stream. Stack and local
inspection read the interpreter's real call frames; `run` always starts a
pristine invocation. Rich arbitrary expression evaluation and process attach
are not implemented.

### Source debugging and VS Code

```bash
nilcompiler --no-bundle --debug-info --output program.nil program.ny
nildebugger --dap
```

`--dap` is a separate Content-Length-framed stdio adapter. The client launches
the compiled NIL file and configures path-qualified breakpoints. It supports
step in/over/out, stack frames, lexically visible named locals (including
shadowing), variable inspection, streamed output, cooperative pause and
termination. Unmapped artifacts explicitly reject source-debug launch.
`--debug-info` retains source paths and is therefore opt-in; binary artifacts
use NIL v5. Source paths are metadata, not authenticated file identities.

Install/build [the VS Code extension](../../vscode/README.md), configure
`ny.sdkPath`, open a saved `.ny`, `.matrix`, or `.mat` file and use F5. The extension compiles with
`--debug-info` into its own temporary directory and starts the SDK's root-level
`nildebugger`; a launch configuration may instead point at precompiled debug
NIL. Pause/terminate take effect between VM instructions, not inside blocking
I/O or a parallel-map join. Interactive stdin, native attach/DWARF, worker
thread stepping and breakpoint hit-count/logpoint support are unavailable.
Binary-only library bodies have no editor-openable source capsule and are
skipped by source stepping; use source-backed `nilstd` imports to debug inside
libraries. A failure in an unmapped function reports its name without inventing
a source path or lexical variable view.

## `nillsp`

`nillsp` communicates over standard input and output using LSP framing:

```text
nillsp
```

It has no command-line transport flags. The server advertises full-text document
synchronization, completion, go-to-definition, hover, and hierarchical document
symbols. Positions and replacement edits use LSP UTF-16 coordinates, including
non-BMP characters. It publishes lightweight structural diagnostics when
documents open or change; these do not replace compiler diagnostics.

Completion uses a tolerant token/declaration index, not a static keyword list.
It understands `fn`/`function`, parameters, local declaration order, lexical
shadowing, branch/loop scopes, module sections, structs/classes, and impl methods
in the requested open document. It excludes comments, strings, other functions'
locals, and unrelated open files. A known annotated or constructor-inferred
receiver offers declared fields and methods; `ref<T>` receivers and the
declaration members of generic types are supported. Hover and definition use
the same scope and receiver lookup. Incomplete functions remain editable.

This is document-local semantic support, not whole-workspace compiler type
inference. Import/namespace resolution, qualified or chained receiver inference,
generic member-type substitution, references,
rename, formatting, and TCP transport remain unimplemented. Unknown receivers
produce no guessed member list. The first-party VS Code integration is available
as source and a locally packaged VSIX (`npm run package` in `vscode`). Matrix
has a separate tolerant declaration index, including document-local inherited
members. See the [extension README](../../vscode/README.md) for the supported
completion and navigation scope of each frontend.

Configure an editor's generic LSP client to launch the absolute `nillsp` path.
Visual Studio Code users can use the first-party integration under
[`vscode`](../../vscode), which also contributes SDK discovery and Ny/Matrix
compile/run tasks.

## Native pipeline commands

`nilassembler` and `nillinker` are always included in a framework build. They expose the
optional LLVM route as separately testable artifact stages; they do not yet
provide separately compiled Ny modules or a frozen cross-target ABI.

### `nilassembler`

```text
nilassembler [-o <output.o>] [--unmanaged] [--embed-context] <input.nil>
nilassembler --version
```

The assembler validates a current compiler-emitted textual or binary executable
NIL module and lowers it through the same LLVM backend used by native compiler
routes. Its output is a genuine host relocatable object (`.o` on POSIX and
`.obj` on Windows), not a Ny-specific container. With no `-o`, it replaces the
input suffix with the host object suffix. NIL libraries, legacy binary magic,
missing backend metadata, and malformed input are rejected. The command reports
a capability error when the SDK was built without LLVM. Safe/runtime-managed
native memory is the default; `--unmanaged` deliberately selects the unchecked
manual-memory lowering used by the compiler's native route.

Current binary executable NIL v3 preserves struct/enum schemas and function
annotations, flags and declaration lines. Class-bearing v4 adds explicit
ancestry and method metadata. The assembler also accepts legacy
v1 executables only where omitted metadata is unnecessary; schema-dependent
operations and annotated intrinsic wrappers in v1 artifacts fail explicitly.
Recompile those artifacts with the current SDK. See the
[format and compatibility contract](../advanced/nil-format.md).

### `nillinker`

```text
nillinker [-o <output>] [--runtime <archive-or-sdk>]
         [--driver-timeout-ms <milliseconds>] <input.o>...
nillinker --version
```

The linker captures each host ELF, Mach-O, or COFF relocatable object and the
runtime archive as bounded, no-follow regular-file snapshots, validates those
snapshots, and gives only private staged copies to the configured host compiler
driver. Driver invocation uses an argument vector without shell construction.
The command accepts at most 4,096 objects, 512 MiB per object, and 4 GiB across
all objects. The driver defaults to a 300,000 ms deadline; the timeout option
accepts 100 through 3,600,000 ms. At the deadline, the linker terminates the
driver process group on POSIX or its Job Object on Windows and confirms cleanup
within a bounded interval. A driver that exits while a contained descendant is
still active is also rejected and cleaned up. Failure to confirm cleanup is an
error and never permits publication. The configured driver is a trusted
toolchain component; POSIX descendants that deliberately create a new session
or close the inherited lifetime descriptor are outside that tracking contract.
This is process lifecycle management, not a sandbox. The POSIX leader remains
unreaped until group signaling finishes, preventing numeric PID reuse from
redirecting cleanup to an unrelated process. The caller's prior output remains
untouched unless a complete executable passes validation.

Windows command-line arguments are recovered from the native UTF-16 command
line, and linker file operations and driver launch stay in the wide-path domain;
non-ASCII object, runtime, staging, and output paths are part of the component
contract. Windows support is currently cross-compiled rather than natively
verified, so Linux remains the supported native-link host.

The linker links every input with the matching `nilruntime` archive and
publishes the executable through private same-directory staging.
`--runtime` may name the archive, its `nillib/native` directory, or an SDK root.
Archive preflight validates bounded GNU/COFF/BSD `ar` framing and requires
symbol-index entries for the versioned runtime ABI anchor names to point at
nonempty, non-metadata members. It does not interpret those member payloads as
host objects; the SDK's executable link-and-run smoke provides the semantic
runtime proof. When omitted, the linker searches sealed SDK, build-tree, and
conventional installed `bin`/library layouts, skipping invalid shadow
candidates. The default output is `a.out` (`a.exe` on Windows); a positional
output path is not accepted. Cross-target linking, shared libraries, library
search flags, and NIL-library object generation are not implemented.

## Experimental commands

Experimental tools are included only when configured with
`-DNIL_BUILD_EXPERIMENTAL_TOOLS=ON`. Their interfaces and file formats can change
without compatibility guarantees.

### `nilschema`

```text
nilschema <input-directory> <output-directory>
```

This legacy extractor scans `.ny` files for an uncommented `@schema` marker,
copies matches while preserving relative paths, and writes
`schema_manifest.txt`. It is separate from the AI context extractor.

### `nilagent`

```text
nilagent
```

`nilagent` is an interactive application-control prototype. It currently reads
JSON-shaped commands from stdin and writes responses to stdout; it does not
open a network port or implement authentication. Window control, screenshots,
hot reload, and interpreter-control connection paths vary by platform and
several paths are stubs.

## Components without shipped commands

- `nilaix` is an experimental source tree, not an SDK command.
- `nypm` is a package-manager proposal; there is no `nypm` executable yet.
- The AI context extractor exists as C++ implementation code under
  `nilcompiler/src`, but it is not currently built as a standalone CLI.
