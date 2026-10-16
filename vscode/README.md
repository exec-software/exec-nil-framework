# NIL Framework (Ny + Matrix) for VS Code

This extension provides editing and toolchain integration for Ny (`.ny`) and
Matrix (`.matrix`, `.mat`). Both languages use the NIL Framework compiler, interpreter,
native backend, and source debugger. The extension launches `nillsp` over
standard input/output for saved and untitled documents in either language.

The installed extension identifier remains `exec-software.ny-language-support`,
and existing `ny.*` settings, commands, task types, and debug configurations keep
working. New SDKs use `nilcompiler`, `nilinterpreter`, `nildebugger`, and `nillsp`;
update explicit executable paths if they still point to the former command names.
The legacy `$nycompiler` problem matcher remains an alias for `$nilcompiler`.

The language server's experimental features remain labeled separately from
the compiler-backed source debugger.

## Features

- TextMate syntax highlighting for canonical Ny syntax and accepted legacy
  spellings.
- Matrix highlighting for case-insensitive keywords, hyphenated and backtick
  names, subject-first declarations, word operators, and multiline text.
- Bracket, quote, comment, indentation, and folding configuration.
- Snippets for modules, functions, data types, control flow, faults, tests,
  interop, documentation, and `@ai_context` metadata.
- A `nillsp` client using standard LSP `Content-Length` framing over stdio.
- Completion, hover, definition, and document outline for Matrix declarations,
  paragraph parameters, local bindings, and known receiver members.
- Compiler-backed checks of current Ny and Matrix buffers, with Problems entries
  and quick fixes for the default style analyzer.
- A `nildebugger --dap` source-debug adapter with source breakpoints, stepping,
  lexical locals, call stacks, incremental output, and cooperative pause.
- Commands and a `ny` Task Provider for:
  - compiling the current file to a separate NIL artifact;
  - compiling and running the current file with `nilinterpreter`;
  - compiling through the optional LLVM native backend;
  - building and installing the workspace SDK; and
  - running the workspace CTest suite.
- Problem matchers for Ny and Matrix compiler diagnostics, including imports
  that use the other language.
- Workspace Trust integration. Syntax support remains available in untrusted
  workspaces, but no executable is launched until the workspace is trusted.

## Requirements

Build or install a NIL Framework SDK containing these commands:

- `nilcompiler`
- `nilinterpreter`
- `nillsp`
- `nildebugger` (for F5 source debugging)

Matrix editing needs a Matrix-aware `nillsp`. Updating the VSIX alone does not
update the SDK binaries. After rebuilding or selecting a newer SDK, run
**NIL Framework: Restart Language Server**. An explicit `ny.server.path` overrides SDK
discovery, so check that setting if an old server still starts.

In the canonical SDK, those executables are directly at the SDK root. A
complete SDK also places `nilassembler`, `nillinker`, `nildebugger`, `nilschema`,
and `nilagent` there, with `nillib`, `nilstd`, `docs`, and `manifest.json` beside
them. The extension launches the compiler, interpreter, language server, and
debugger; its build/test tasks also invoke the configured build tools.

From a NIL Framework repository checkout, the standard SDK build is:

```sh
./scripts/build-sdk.sh
```

The extension discovers each executable in this order:

1. its explicit setting (`ny.server.path`, `ny.compiler.path`,
   `ny.interpreter.path`, or `ny.debugger.path`);
2. the root named by `ny.sdkPath`, then that checkout's `sdk` and
   `build/cmake/bin` developer output;
3. a workspace root, its project-local `sdk`, and its
   `build/cmake/bin` developer output;
4. legacy `build/sdk` and `bin`-based SDK locations as compatibility fallbacks; and
5. the process `PATH`.

The framework checkout publishes its SDK at `<repository>/sdk`, beside source
components such as `nilcompiler/` and `nillsp/`. Source component directories are
never treated as executable files. The legacy `build/sdk` and `bin` fallbacks
are searched after the canonical SDK and current developer build locations.

An invalid explicit tool path is reported instead of silently falling back to a
different executable.

## Commands

Open the Command Palette and run:

- **NIL Framework: Select SDK Folder**
- **NIL Framework: Restart Language Server**
- **NIL Framework: Show Language Server Output**
- **NIL Framework: Check Current Document**
- **NIL Framework: Show Analysis Output**
- **NIL Framework: Compile Current File to NIL**
- **NIL Framework: Compile and Run Current File**
- **NIL Framework: Compile Current File Natively**
- **NIL Framework: Build and Install Workspace SDK**
- **NIL Framework: Run Workspace CTest Suite**

Compile-and-run performs two separate task executions. Compilation always uses
`--no-bundle`, producing `<source-name>.nil` beside the source by default, and
only starts `nilinterpreter` after a successful compiler exit.

These source commands accept saved `.ny`, `.matrix`, and `.mat` files in the
matching language mode. Matrix selects the same shared compiler pipeline and
supports both NIL/VM execution and the optional LLVM native backend.

The SDK and CTest commands target a NIL Framework repository checkout. The SDK command
runs `scripts/build-sdk.sh`; the CTest command uses `ny.buildDirectory`, which
defaults to `build/cmake`.

## Tasks

The `ny` task type accepts `compile`, `run`, `native`, `build-sdk`, and `ctest`.
For example:

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "Compile current Ny or Matrix file",
      "type": "ny",
      "task": "compile",
      "source": "${file}",
      "output": "${fileDirname}/${fileBasenameNoExtension}.nil",
      "problemMatcher": ["$nilcompiler", "$matrixcompiler"],
      "group": "build"
    },
    {
      "label": "Run current NIL file",
      "type": "ny",
      "task": "run",
      "output": "${fileDirname}/${fileBasenameNoExtension}.nil"
    }
  ]
}
```

The `run` task executes an existing NIL artifact. Use **NIL Framework: Compile and Run
Current File** when both steps should happen in sequence.

Compiler task definitions also accept `sdkPath`, `noStdlib`, and `args`. Run
tasks use `args` for `nilinterpreter` options and `programArgs` for arguments to
the Ny or Matrix program; the extension inserts the required `--` separator. The
extension never adds `--no-typecheck`; type checking remains part of normal
editor builds.

## Settings

| Setting | Default | Purpose |
| --- | --- | --- |
| `ny.languageServer.enabled` | `true` | Start `nillsp` for Ny and Matrix files. |
| `ny.languageServer.diagnostics.enabled` | `false` | Show the server's experimental structural diagnostics. |
| `ny.analysis.enabled` | `true` | Check current buffers with the compiler, including unsaved changes. |
| `ny.analysis.includePaths` | `[]` | Additional import roots for editor checks. |
| `ny.sdkPath` | empty | Canonical SDK root or NIL Framework repository root. |
| `ny.server.path` | empty | Explicit `nillsp` path or command. |
| `ny.compiler.path` | empty | Explicit `nilcompiler` path or command. |
| `ny.interpreter.path` | empty | Explicit `nilinterpreter` path or command. |
| `ny.debugger.path` | empty | Explicit `nildebugger` path or command. |
| `ny.compiler.noStdlib` | `false` | Add `--no-stdlib` to compiler tasks and editor checks. |
| `ny.compiler.extraArgs` | `[]` | Extra arguments for every compiler task. |
| `ny.native.extraArgs` | `[]` | Extra arguments for native compiler tasks. |
| `ny.outputDirectory` | empty | Artifact directory; empty means beside the source file. |
| `ny.buildDirectory` | `build/cmake` | Build tree passed to CTest. |
| `ny.trace.server` | `off` | Trace LSP messages in the server output channel. |

Path settings support `${workspaceFolder}`, `${workspaceFolderBasename}`,
`${env:NAME}`, and `~`.

## Compiler checks and quick fixes

The extension checks saved and untitled Ny and Matrix documents with
`nilcompiler --analyze --source-stdin <logical-source-path>`. It sends the current
buffer over standard input and creates no compiled output. Checks run after a
400 ms editing pause, one compiler at a time; a new edit cancels the previous
check. Each check is limited to 10 seconds, 2 MiB of source, and 8 MiB of output.
An SDK supporting the `nil.analysis.v1` protocol is required. Older compilers and
operational failures are reported in **NIL Framework: Show Analysis Output**.

Compiler errors and default analyzer warnings appear with source `nil-analysis`
in Problems, separately from the experimental language-server diagnostics.
The default analyzer checks trailing whitespace and missing final newlines.
Its quick fixes only apply to the exact document version that was checked.
Compiler spans use UTF-8 byte offsets; the extension converts valid boundaries
to editor positions without guessing locations. Findings in imported files or
without a reliable location appear in the analysis output channel.

Checks preserve the source's parent directory, workspace root, configured SDK
root, and `ny.analysis.includePaths` for import lookup. Untitled buffers use a
synthetic filename under the workspace root, without writing that file. Relative
include paths resolve against the owning workspace folder. Unsaved edits in
other imported buffers are not overlaid; save those files to recheck dependents.
`ny.compiler.extraArgs` and `ny.native.extraArgs` apply to build tasks only.
Editor checks are disabled in untrusted workspaces and can be switched off with
`ny.analysis.enabled`.

## Current language-server limits

`nillsp` uses a tolerant token/declaration index that is separate from the
compiler frontend. Completion, definition, and hover understand lexical scopes,
declaration order, shadowing, and known struct/class receivers within the
requested open document. They do not yet resolve imports or provide whole-project
or standard-library inference, full generic type inference, references, rename,
formatting, semantic tokens, signature help, or code actions.

Matrix completion matches names without regard to case and replaces the whole
typed prefix, including hyphens. It preserves the declaration's spelling and
does not offer symbols inside comments or text literals. Incomplete paragraphs
can still provide completion while being edited. Locally declared aliases,
base classes, implementations, and extensions contribute known members;
unknown receivers do not produce guessed fields. Contextual suggestions include
Matrix type names, keywords, and word-operator phrases. Snippets supply common
declaration and control-flow forms.

Structural diagnostics are disabled by default because that parser does not
understand every canonical construct, including all uses of `fn`. Turn on
`ny.languageServer.diagnostics.enabled` only when evaluating the current
experimental diagnostics.

The compiler's parse diagnostics include a source path and line and therefore
feed the extension's `$nilcompiler` and `$matrixcompiler` problem matchers. Matrix
locations may include a column. Current type-check and
code-generation diagnostics do not consistently include a source filename, so
the matcher deliberately does not guess their location.

## Source debugging

Open a saved Ny or Matrix source and press F5, selecting **NIL Framework source debugger**
if asked.
The workspace must be trusted. The extension resolves `nilcompiler` and
`nildebugger` from `ny.sdkPath`, workspace builds, or PATH; `ny.debugger.path`
overrides the debugger executable. It compiles source with `--debug-info`
into a fresh temporary directory and removes that directory after the session.
Compilation errors abort launch; a stale artifact is never reused.

```json
{
  "version": "0.2.0",
  "configurations": [{
    "type": "ny", "request": "launch", "name": "Debug Ny",
    "program": "${workspaceFolder}/main.ny",
    "cwd": "${workspaceFolder}", "args": [], "stopOnEntry": true,
    "memoryMode": "safe"
  }]
}
```

`program` accepts `.ny`, `.matrix`, `.mat`, or a `.nil` compiled with
`--debug-info --no-bundle`.
Source metadata carries absolute paths and lexical names; keep that in mind
before distributing debug artifacts. Breakpoints require the exact source file
and an executable line. Conditional breakpoints support a local/global name
or one field, a comparison, and a scalar value; arbitrary expressions, hit
counts, and logpoints are not supported. Inner lexical shadows are resolved
against the paused instruction, and expired scopes disappear from Locals.
Use a source-backed SDK (`nilstd` present) to step into library functions.
Binary-only library bodies have no editor-openable source and are skipped by
source step-in; a failure inside one shows its function frame without a guessed
source file. Embedded-library source retrieval is not implemented.

Matrix source breakpoints work, but Locals and Watch currently expose the
lowered binding names: a source binding `local-count` appears as `local_count`,
and Watch must use that displayed spelling. Preserving original Matrix spelling
and case-insensitive name lookup in debugger expressions remains follow-up work.

Pause is cooperative between VM instructions. Blocking FFI, sleeps, and a
parallel-map instruction finish before stopping; parallel workers are not
separate debugger threads. Native executables/DWARF/attach, interactive program
stdin, and variable mutation remain separate future work. `nildebugger --json`
is still available for the older synchronous JSON Lines automation interface.

## Extension development

```sh
cd vscode
npm ci
npm test
npm run test:host
npm run build
npm run package
```

The unit suite runs under Node without launching Electron. It type-checks the
extension, exercises tool discovery and task construction, validates package
assets and snippets, and compiles/tokenizes the TextMate grammar with Oniguruma.

`npm run test:host` launches the locally installed `code` in an isolated profile
and temporary workspace. Build the tools in `build/cmake` first; this suite
requires LLVM for native compilation, the analysis CLI, and a desktop display on Linux. It checks
Matrix completion/navigation in saved and untitled documents, compile/run and
native commands, source debugger entry/cleanup for `.matrix` and `.mat`, and
compiler analysis of unsaved Ny and Matrix buffers with version-guarded fixes.
Set `VSCODE_EXECUTABLE` or `NIL_VSCODE_TOOL_DIRECTORY` to override the editor or
tool directory (`NY_VSCODE_TOOL_DIRECTORY` is accepted for compatibility).
Its report is written to `build/vscode-host-smoke/latest.json`;
the runner prints the temporary profile/log location for troubleshooting.

The build bundles the extension into `dist/extension.js` with esbuild. The VSIX
expects a separately installed NIL Framework SDK; native binaries are not embedded.

## License

MIT License. See [LICENSE](LICENSE).

Copyright (C) 2008 - 2026 Execute Software.  All rights reserved.

See the [project contributors](https://github.com/exec-software/exec-nil-framework/blob/master/CONTRIBUTORS.md)
for original and community credits.

Bundled dependencies retain their own licenses; see [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
