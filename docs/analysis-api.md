# Compiler analysis and reflection

Ny and Matrix use the same read-only compiler analysis pipeline. It parses the
primary source, resolves source or binary-library imports, and runs the shared
semantic and ownership checks. It does not generate or execute a program.

```sh
sdk/nilcompiler --analyze -I sdk/nilstd application.matrix
sdk/nilcompiler --analyze --source-stdin -I sdk/nilstd application.matrix < buffer.txt
sdk/nilcompiler --analyze -I sdk/nilstd application.matrix \
  --emit-reflection analysis_snapshot.ny
```

The source path selects the frontend (`.ny`, `.matrix`, or `.mat`, case
insensitive). With `--source-stdin`, it is the logical editor filename; the file
need not exist. The buffer replaces that file only; imports are read from disk.
The source's directory participates in import lookup. `-I`, `--sdk`,
`--sdk-path`, and `--no-stdlib` retain their compiler meanings. Execution,
code-generation, output, and type-check bypass flags are rejected.

## Diagnostic protocol

Standard output contains one UTF-8 JSON object with schema `nil.analysis.v1`:

```json
{
  "schema": "nil.analysis.v1",
  "source": "/project/application.matrix",
  "language": "matrix",
  "success": true,
  "diagnostics": [],
  "sources": [],
  "reflection": {
    "schema": "nil.reflection.v1",
    "semantic_checked": true,
    "truncated": false,
    "declarations": [],
    "annotations": []
  }
}
```

Each diagnostic has `code`, `severity`, `message`, `path`, `start`, `length`,
`hint`, and `fix`. `start` and `length` are **UTF-8 byte offsets**, not UTF-16
editor positions. A fix is either null or
`{start, length, replacement, description}`. Unavailable paths/spans are null.
The primary path preserves the caller's normalized absolute identity, including
symlink spellings. Imported sources use their resolved paths.

Exit status 0 means checking succeeded; warnings do not fail checking by default.
Status 1 means source/semantic/import errors or warnings promoted to errors,
with a valid JSON report. Status 2 means
invalid options, invalid UTF-8, input/output limits, or an operational failure;
these report on standard error without a JSON payload.

The built-in diagnostic codes are:

| Code | Meaning |
| --- | --- |
| `NIL0000` | Compiler failure without a more specific structured record |
| `NIL0001` / Matrix parser codes | Syntax errors |
| `NIL0002` | Shared type/ownership checking errors |
| `NIL0003` | Unresolved import |
| `NIL0004` | Diagnostic output exceeded the analysis limit |
| `NIL1001` | Trailing whitespace; deletion fix |
| `NIL1002` | Missing final newline; insertion fix preserving CRLF style |
| `NIL2001` | Unreachable statement after an unconditional exit |
| `NIL2002` | Discarded standard `Option` or `Result` outcome |

Style checks run after successful semantic checking. Matrix raw-text ranges
come from the Matrix lexer; whitespace inside literal content is preserved.
Parser diagnostics may have precise spans. Shared checker diagnostics currently
have line-wide spans, and older ownership/contract checks may have no file
provenance. Textual Ny `@include` expansion has no source map yet: its spans and
reflected lines are withheld rather than attributed to the wrong source line.

The default analyzer also runs two conservative rules on the checked primary
source: `NIL2001` reports unreachable statements after unconditional exits, and
`NIL2002` reports standalone calls whose standard `Option`/`Result` outcomes
are discarded. These are warnings without automatic fixes. Imported modules
receive compiler checks but are not recursively linted by these rules.
These rules traverse named function/method and test bodies; expression-embedded
lambda bodies remain outside this initial traversal.

`nilcompiler --list-analysis-rules` returns a `nil.analyzers.v1` catalog with
the `nil.default` analyzer and each rule's code, title, description, default
severity, phase and fix availability. `--analyze --disable-rule CODE` suppresses
a known default rule; repeat the option to suppress more than one. Compiler
errors cannot be suppressed. `--warnings-as-errors` promotes the remaining
warnings to errors and returns status 1 with `success: false`. The reflection
`semantic_checked` flag still records whether semantic checking itself passed.
The [project driver](tools/projects.md) applies the same policy before builds.

The additive `sources` array contains `{path, text}` snapshots of imported
sources referenced by located diagnostics. It excludes the primary buffer and
is capped at 1 MiB of encoded JSON entries; omitted files remain unmapped by
the editor. These are exact analyzed texts, allowing an editor to reject stale
locations rather than assuming offsets apply to a subsequently modified file.

Input units are limited to 2 MiB each and 256 units. Diagnostic count is capped
at 512 with a 256 KiB message budget. Reflection retains at most 8,192
declarations/annotations and 1 MiB of metadata, and marks truncation explicitly.
The full JSON response is limited to 8 MiB. These are representation limits,
not a compiler execution sandbox or time budget.

## Reflection from Ny or Matrix

`nil.std.reflection` exposes immutable value records:

- `declaration_info`: `kind`, `name`, `qualified_name`, `type_name`,
  `source_path`, `line`, `parent_id`, `is_public`, `semantic_checked`.
- `annotation_info`: `owner_id`, `name`, `arguments`, `line`.

The JSON snapshot assigns each declaration an `id`. IDs are local to one
snapshot; never persist them across edits. A missing parent is `-1`, an
unavailable line is `0`. Declaration kinds include functions, parameters,
records, fields, classes, traits, implementations, enums, variants, type aliases,
and globals. Generated generic specializations are excluded. This first API
contains declaration metadata, not expression trees, reference bindings, a
control-flow graph, or runtime object reflection. `semantic_checked` means the
whole compilation passed checking; it does not promise that every type string
has been fully elaborated.

Successful `--emit-reflection` writes an ordinary Ny module named
`analysis_snapshot`, atomically, with these exported functions:

```text
count() -> i32
declaration(id: i32) -> declaration_info
annotation_count() -> i32
annotation(id: i32) -> annotation_info
```

Invalid IDs panic. Truncated snapshots cannot be exported. A failed analysis
does not replace an existing snapshot, so build scripts must check exit status.
The generated module can be consumed by either frontend on VM or native routes.

The [example Matrix analyzer](../nilstd/examples/analyzer.matrix) iterates the
snapshot and emits diagnostic JSON for temporary names in public functions:

```sh
sdk/nilcompiler --analyze application.matrix -I sdk/nilstd \
  --emit-reflection analysis_snapshot.ny > analysis.json
sdk/nilcompiler nilstd/examples/analyzer.matrix analysis_snapshot.ny \
  -I sdk/nilstd --no-bundle -o analyzer.nil
sdk/nilinterpreter analyzer.nil
```

Library helpers include `is_function`, `is_record`, `is_public_api`,
`has_checked_semantics`, and `diagnostic_json`. The latter formats a standalone
line-based diagnostic, not the full editor envelope. Custom analyzers are
explicit programs today; the editor does not automatically load or execute
analyzer modules. Compiler extension callbacks, symbol-reference queries,
unused-symbol analysis, a full control-flow graph, and a versioned
custom-analyzer host remain next steps. Default-rule suppression and
warnings-as-errors are available as described above; this does not enable
arbitrary plugin loading.

## Visual Studio Code

The extension runs the built-in analyzer for saved and unsaved Ny/Matrix buffers
in trusted workspaces. `ny.analysis.enabled` defaults to true. Results have their
own Problems collection. Imported diagnostics appear in Problems only when
their bounded source snapshot matches the current file and the imported editor
buffer is not dirty; remaining imported/unlocated records go to the analysis
output channel. It cancels superseded work, debounces edits, limits
process time/output, validates byte boundaries, and applies fixes only to the
same document version that produced them. Compiler `extraArgs` are deliberately
not forwarded to analysis. Existing structural LSP diagnostics remain separate.

Within a `nil.project.json` project, live checks inherit project import roots,
SDK location and rule policy. Project build/run tasks use `nilbuild`. The
primary unsaved buffer is still the only overlay; unsaved imports and a shared
semantic workspace index remain follow-up work.

Use a current SDK/compiler. The extension prefers the repository's `sdk` tools;
a stale SDK can otherwise mask a newer build-tree compiler.
