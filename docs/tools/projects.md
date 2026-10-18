# Projects and the default analyzer

`nilbuild` checks, builds and runs a local Ny/Matrix project using the installed
compiler. A `nil.project.json` file supplies the same entrypoint, import roots,
SDK selection and analyzer policy to the command line and the VS Code extension.
Project execution is currently supported on Linux; other hosts are not yet
qualified. The native target requires an LLVM-enabled compiler and a host C++
linker.

## Create a project

Save this file as `nil.project.json` beside a `src` directory:

```json
{
  "schema": "nil.project.v1",
  "name": "hello",
  "entry": "src/main.matrix",
  "includePaths": ["src"],
  "outputDirectory": "build",
  "analysis": {
    "disabledRules": [],
    "warningsAsErrors": false
  }
}
```

The entrypoint can be `.ny`, `.matrix` or `.mat`. Imported files can use either
frontend. The [mixed-language example](../../examples/projects/hello-mixed/nil.project.json)
uses a Matrix entrypoint and a Ny module.

With the SDK on `PATH`, run from the project directory:

```sh
nilbuild describe
nilbuild check
nilbuild build
nilbuild run
nilbuild run --target native
```

`build` and `run` check the project first. A compiler error or a promoted
analyzer warning stops the build; `run` does not execute an old artifact after
a failed check or compile. Successful compilation replaces the selected output
only after a complete staged artifact exists. `run` rebuilds each time. There
is no timestamp or incremental cache in this version.

## Project contract

| Field | Meaning |
| --- | --- |
| `schema` | Required, exactly `nil.project.v1`. |
| `name` | Required artifact basename, at most 128 ASCII characters; starts with a letter or digit and otherwise uses letters, digits, `.`, `_` or `-`. It cannot end in a source extension. |
| `entry` | Required Ny or Matrix source entrypoint. Its imports define the compilation graph. |
| `includePaths` | Optional array of import roots. The project directory is always included. |
| `sdkPath` | Optional compiler SDK/import location. A CLI `--sdk` override takes precedence. |
| `outputDirectory` | Optional output directory, default `build`. |
| `analysis.disabledRules` | Optional array of distinct default-analyzer rule codes. Compiler errors cannot be disabled. |
| `analysis.warningsAsErrors` | Optional Boolean, default `false`; remaining warnings fail checking when enabled. |

Relative manifest paths resolve against the manifest directory, independent of
the current working directory. Project files are UTF-8 JSON, limited to 1 MiB
and 32 levels of nesting. Duplicate keys, unknown fields and invalid types are
errors. Project files contain configuration, not executable build scripts.

This version describes one executable and its source imports. It does not
define package resolution, remote downloads, arbitrary hooks, a multi-project
target graph, test discovery or publishing. Those require separate contracts;
the [package-manager proposal](../design/nypm.md) remains a proposal.

## Commands

```text
nilbuild describe [--project FILE] [--format json]
nilbuild check    [--project FILE] [--format json]
nilbuild build    [--project FILE] [--target vm|native]
nilbuild run      [--project FILE] [--target vm|native] [-- PROGRAM_ARGUMENTS...]
```

Without `--project`, the driver searches the current directory and its
ancestors for `nil.project.json`. Tool overrides are `--compiler PATH` and
`--interpreter PATH`; otherwise the driver looks beside itself and then on
`PATH`. `--sdk PATH` resolves relative to the invocation directory and overrides
the manifest's SDK location.

`describe --format json` emits `nil.project.description.v1` with resolved paths
and settings. `check --format json` emits the compiler's
[`nil.analysis.v1`](../analysis-api.md) report. Human-readable checks include
diagnostic codes and locations. The VM output is `build/<name>.nil`; native
output is `build/<name>`, with the configured output directory substituted.
Program arguments are passed as separate arguments without shell evaluation.

Compiler invocations have a five-minute timeout and a combined 16 MiB output
limit. After a successful build, `run` replaces the driver with the program or
interpreter, preserving normal terminal input, signals and exit status without
those limits. Check/build never execute the project's program; `run` explicitly
does.

## Default analyzer

The analyzer ships inside `nilcompiler`. Discover its current rule metadata:

```sh
nilcompiler --list-analysis-rules
nilcompiler --analyze src/main.matrix -I src
nilcompiler --analyze src/main.matrix -I src --disable-rule NIL1001 --warnings-as-errors
```

| Rule | Default warning |
| --- | --- |
| `NIL1001` | Trailing whitespace; includes a safe deletion fix. |
| `NIL1002` | Missing final newline; includes a newline insertion fix. |
| `NIL2001` | Unreachable statements after an unconditional exit. |
| `NIL2002` | A call returns a standard `Option` or `Result`, but its outcome is discarded. |

Semantic rules run only after successful type and ownership checking. They
inspect the entry source; imported sources still receive compiler checks.
The current traversal covers named functions, methods and test blocks, including
their nested statement blocks; expression-embedded lambda bodies are not yet
analyzed by these two rules.
Unreachable-code analysis is conservative structured analysis, not a complete
control-flow graph. The discarded-result rule uses resolved types rather than
matching a function name. Semantic warnings do not offer an automatic edit:
deleting code or inventing error handling would require a programmer's decision.

Direct compilation remains compatible with existing compiler invocations.
Use `--analyze` for compiler-only checking, or `nilbuild` to require the analyzer
before every project build. Third-party Matrix/Ny analyzer programs remain
explicit programs; this driver does not automatically load plugins.

## Visual Studio Code

Open the project folder and select a current SDK. The extension discovers the
nearest project file within the workspace folder, offers project check/build/run
commands, and uses the same SDK, import roots and analyzer policy for live
checks. Editing the manifest updates that context. JSON completion and
validation describe the project fields.

Compiler errors in imported files appear in Problems when their analyzed source
snapshot still matches the file on disk. A dirty imported editor buffer is not
treated as the source used by the compiler. The primary buffer is analyzed
without saving; unsaved imported-buffer overlays remain future work. Imported
diagnostics without a matching bounded snapshot remain in the analysis output.
Quick fixes remain limited to the unchanged primary document version.

This establishes project-aware checking and build tasks. Completion, hover and
local navigation still use the language server's tolerant document index. Full
semantic navigation, cross-file references, rename and signature help are
tracked in the [developer-tooling plan](../developer-tooling.md).
