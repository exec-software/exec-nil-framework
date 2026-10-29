# Projects and the default analyzer

`nilbuild` checks, builds and runs Ny/Matrix projects using the installed
compiler. A `nil.project.nildata` manifest supplies the entrypoint, import
roots, SDK selection and analyzer policy to the CLI and VS Code extension.
Each project produces one executable; project references express the ordering
between projects. Execution is currently qualified on Linux. The native target
requires an LLVM-enabled compiler and a host C++ linker.

## Create a project

For an existing source entrypoint, create its manifest with:

```sh
nilbuild init --name hello --entry src/main.matrix
```

This writes `nil.project.nildata` in the current directory. It creates only the
manifest: create `src/main.matrix` yourself or point `--entry` at existing Ny or
Matrix source. Existing files are never overwritten. Use `--project FILE` to
choose the manifest location.

A manifest can also be written directly:

```nildata
nildata 1
object
  schema is "nil.project.v1"
  name is "hello"
  entry is "src/main.matrix"
  includePaths is an array
    item is "src"
  end array
  outputDirectory is "build"
  analysis is an object
    disabledRules is an array
    end array
    warningsAsErrors is false
  end object
end object
```

The entrypoint can be `.ny`, `.matrix` or `.mat`. Imported files can use either
frontend. The [mixed-language example](../../examples/projects/hello-mixed/nil.project.nildata)
uses a Matrix entrypoint and a Ny module.

With the SDK on `PATH`, run from the project directory:

```sh
nilbuild describe
nilbuild check
nilbuild build
nilbuild run
nilbuild run --target native
nilbuild build --target all --jobs 4
```

`build` and `run` check the saved import graph on every invocation. Compiler
errors and promoted analyzer warnings stop the affected build; `run` never
executes an old artifact after a failed check or compile. When the checked
inputs, toolchain, settings and existing output match a previous successful
build, compilation and linking are skipped. Otherwise, compilation writes to a
private staging directory and replaces the output only after success.

## Project contract

| Field | Meaning |
| --- | --- |
| `schema` | Required, exactly `nil.project.v1`. |
| `name` | Required artifact basename, at most 128 ASCII characters; starts with a letter or digit and otherwise uses letters, digits, `.`, `_` or `-`. It cannot end in a source extension. |
| `entry` | Required Ny or Matrix source entrypoint. Its imports define the compilation graph. |
| `includePaths` | Optional array of import roots. The project directory is always included. |
| `projectReferences` | Optional array of paths to project manifests that must build first. Paths are relative to this manifest. |
| `sdkPath` | Optional compiler SDK/import location. A CLI `--sdk` override takes precedence. |
| `outputDirectory` | Optional output directory, default `build`. |
| `analysis.disabledRules` | Optional array of distinct default-analyzer rule codes. Compiler errors cannot be disabled. |
| `analysis.warningsAsErrors` | Optional Boolean, default `false`; remaining warnings fail checking when enabled. |

Relative paths resolve against the manifest directory, independent of the
current working directory. Documents must be UTF-8, no larger than 1 MiB, with
at most 32 nesting levels and 65,536 values. Duplicate keys, unknown fields and
invalid types are errors. These are declarative documents: loading one does not
execute code, expand environment variables or evaluate interpolation.

A project reference establishes build ordering. It does not add an import
root, expose the referenced program's symbols, or link its executable into the
caller. Configure `includePaths` for shared source modules as before. A project
shared by several dependents is built once; each manifest must list distinct
reference paths. Reference cycles are errors. Package resolution, remote downloads, arbitrary hooks, test discovery
and publishing remain outside this profile.

## Incremental and parallel builds

`--jobs N` limits simultaneous compiler processes to 1–64; the default uses up
to four hardware threads. Independent projects in a dependency wave can be
checked and compiled concurrently. References complete before their dependents
are built. `--target all` builds both VM and native outputs and allows the two
compilations to run concurrently; it is available only with `build`. `run`
selects one target, and still builds that project's references first.

Parallelism is across projects and output targets. A single executable's
imported modules are still compiled together by one compiler process; this
version does not provide separate object compilation or parallel parsing inside
that process. A single-project VM build therefore has one compilation job.

Incremental state lives in `<outputDirectory>/.nilbuild/` as NILData documents.
This directory is reserved: project inputs and artifact destinations cannot
occupy it, including through another project reference.
Its content identities use SHA-256, including the compiler-reported source,
textual include and binary-library inputs, effective settings and toolchain,
and the produced artifact. Native builds also account for the resolved runtime
archive, linker driver, startup objects and system link-library files. Unknown
native linker layouts disable cache reuse. Modifying a source while preserving its timestamp,
changing import resolution, replacing a tool, or changing the existing artifact
invalidates reuse. A missing or malformed state file is a cache miss.

Analysis remains fresh even when compilation is skipped. It supplies current
diagnostics and resolves the import graph again, so a newly created source
that shadows an earlier import is observed. A compiler that does not certify
complete input dependencies remains usable; its projects are rebuilt each time.
Use `--rebuild` to force compilation and linking without deleting outputs:

```sh
nilbuild build --rebuild --target all --jobs 2
nilbuild run --rebuild
```

Concurrent builds that claim the same outputs fail with a lock diagnostic;
they do not wait for each other. Failed or cancelled compiler jobs retain the
previous artifact and remove their staging directories. Successful independent
projects may already have published outputs when another job fails: a project
graph is not one atomic filesystem transaction. Keep inputs stable during a
build; this version does not provide a filesystem snapshot of the workspace or
of the host linker installation.

## Manifest formats and migration

NILData is the default for manifests, build state and `describe` output. JSON
and YAML project files remain readable and explicitly writable. Their use as
the default project format is deprecated; creation/conversion to either format
emits guidance to prefer NILData. This concerns `nilbuild` configuration only.
The JSON/YAML standard-library APIs, compiler/editor protocols, SDK and release
manifests, and VS Code's own settings/tasks/launch files keep their formats.

At each directory, automatic discovery tries `nil.project.nildata`, then
`nil.project.json`, `nil.project.yaml` and `nil.project.yml`. The nearest directory
with a supported manifest wins. Explicit `--project FILE` selects a particular
file. `.nildata`, `.yaml` and `.yml` suffixes select their respective parsers;
other explicit filenames retain the legacy JSON interpretation.

Convert an existing manifest without overwriting it:

```sh
nilbuild convert --project nil.project.json --output nil.project.nildata
nilbuild convert --project nil.project.nildata --output compatibility.json --format json
nilbuild init --name hello --entry src/main.matrix --project nil.project.yaml --format yaml
```

Conversion validates the project schema and stays in the same project directory
to preserve implicit import lookup and the program working directory. Its
destination must not already exist. Comments and
original formatting are not preserved. The default conversion format is
NILData; select a matching suffix when requesting JSON or YAML output.

The YAML compatibility reader supports one document with space-indented block
mappings and sequences, plain and single-quoted text, JSON double-quoted text,
JSON numbers, `true`, `false`, `null` and `~`. Single-quoted text escapes an
apostrophe by doubling it. Inline arrays and objects must use JSON syntax.
Blank lines, comments and optional `---`/`...` document markers are supported.
Numeric-looking text that is not a JSON number remains text; dates, octal and
YAML 1.1 Boolean spellings are not implicitly converted. Tags, anchors, aliases,
merge keys, block strings, tab indentation and multiple documents are rejected.
This bounded project reader does not define or restrict the standard library's
YAML functionality.

## Commands

```text
nilbuild init     --name NAME --entry FILE [--project FILE] [--format nildata|json|yaml]
nilbuild convert  --project FILE --output FILE [--format nildata|json|yaml]
nilbuild describe [--project FILE] [--format nildata|json|yaml]
nilbuild check    [--project FILE] [--format nildata|json|yaml]
nilbuild build    [--project FILE] [--target vm|native|all] [--jobs N] [--rebuild]
nilbuild run      [--project FILE] [--target vm|native] [--jobs N] [--rebuild] [-- PROGRAM_ARGUMENTS...]
```

Tool overrides are `--compiler PATH` and `--interpreter PATH`; otherwise the
driver looks beside itself and then on `PATH`. `--sdk PATH` resolves relative
to the invocation directory and overrides the manifest's SDK location.

`describe` emits `nil.project.description.v1` with resolved paths and settings.
`check` presents diagnostic codes and locations by default; selecting `--format`
emits the compiler's [`nil.analysis.v1`](../analysis-api.md) report in that
notation. NILData/YAML reports use the same 1 MiB and 65,536-value bounded
document profile; JSON reports retain the compiler capture limit. Existing
`--format json` consumers remain supported. The VM output is
`build/<name>.nil`; the native output is `build/<name>`, with the configured
output directory substituted. Program arguments are separate process arguments
and are never evaluated as shell text.

Compiler invocations have a five-minute timeout and a combined 16 MiB output
limit per child. Cancellation reaches active compiler process groups and stops
new jobs. After a successful build, `run` replaces the driver with the program
or interpreter, preserving terminal input, signals and exit status without
those compiler limits. Check/build never execute the project's program.

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
The traversal covers named functions, methods, test blocks and nested lambda
bodies, including switches in expression position.
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
checks. Editing any supported manifest format updates that context. NILData
files have dedicated syntax highlighting; legacy JSON manifests retain schema
completion and validation.

Live analysis checks the primary buffer together with open source dependencies,
including unsaved edits. Compiler errors in imported files appear in Problems
when their analyzed source snapshot still matches the captured editor buffer
or current disk text for unopened files. Input changes invalidate pending
results. Imported diagnostics without a matching bounded snapshot remain in
the analysis output. Quick fixes remain limited to the unchanged primary
document version. Project tasks use saved files; save source changes before
building or running.

Go to Definition uses the compiler's checked source bindings for supported
variables, parameters and direct/imported functions, including unsaved sources.
Compiler completion supplies visible locals, parameters and imported functions
at supported identifier expressions, including unresolved prefixes. Nonempty
member prefixes use the checked receiver type to suggest available source-backed
fields and methods. Signature help follows supported resolved calls, including
imports, methods and immutable callbacks, and maps Matrix named arguments to
the selected callable's parameter order. Both queries validate every consumed
source snapshot before displaying results. Parse recovery remains limited;
the language server retains tolerant completion for unsupported contexts and
supplies hover and outline information.

Find All References and rename are available for compiler-certified lexical
locals; rename checks the proposed source and its bindings before returning
edits.

Live source checks use the active document as their analysis root. A manifest
shares configuration with those checks, but does not turn them into a complete
index of all callers in the project. Project-scoped references and multi-file
rename remain proposed in
[NIL-0013](../design/NIL-0013-project-semantic-editing.md). Further completion
and navigation coverage is tracked in the
[developer-tooling plan](../developer-tooling.md).
