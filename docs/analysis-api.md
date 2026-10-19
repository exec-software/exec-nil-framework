# Compiler analysis and reflection

Ny and Matrix use the same read-only compiler analysis pipeline. It parses the
primary source, resolves source or binary-library imports, and runs the shared
semantic and ownership checks. It does not generate or execute a program.

```sh
sdk/nilcompiler --analyze -I sdk/nilstd application.matrix
sdk/nilcompiler --analyze --source-stdin -I sdk/nilstd application.matrix < buffer.txt
sdk/nilcompiler --analyze --semantic-index -I sdk/nilstd application.matrix
sdk/nilcompiler --analyze -I sdk/nilstd application.matrix \
  --emit-reflection analysis_snapshot.ny
```

The source path selects the frontend (`.ny`, `.matrix`, or `.mat`, case
insensitive). With `--source-stdin`, it is the logical editor filename; the file
need not exist. The raw buffer replaces that file only; imports are read from
disk. For multiple unsaved source buffers, use the workspace request below.
The source's directory participates in import lookup. `-I`, `--sdk`,
`--sdk-path`, and `--no-stdlib` retain their compiler meanings. Execution,
code-generation, output, and type-check bypass flags are rejected.

## Workspace source requests

`--workspace-stdin` reads a versioned JSON request instead of raw source. It is
mutually exclusive with `--source-stdin`; the command-line source remains the
primary file being checked.

```sh
sdk/nilcompiler --analyze --workspace-stdin -I /project \
  /project/main.matrix < workspace.json
```

```json
{
  "schema": "nil.analysis.workspace.v1",
  "sources": [
    {
      "path": "/project/main.matrix",
      "text": "book sample\nread everything from helper\nmain returns override integer begin paragraph\n    give (answer with nothing)\nend paragraph\n"
    },
    {
      "path": "/project/helper.ny",
      "text": "module helper\npub fn answer() -> i32\n return 0\nend fn\n"
    }
  ]
}
```

Each path is absolute and ends in `.ny`, `.matrix` or `.mat`. The request must
include the primary source. Existing paths and symlinked parent directories
resolve to canonical identities; duplicate identities are errors, including
two spellings of the same file. The primary diagnostic identity still preserves
the caller's logical filename. Files need not exist on disk.

The request is strict UTF-8 JSON: duplicate keys, unknown fields, invalid types
and NUL bytes are rejected. Limits are 256 sources, 2 MiB per source, 8 MiB for
the encoded request and decoded source aggregate, and 4,096 UTF-8 bytes per path.
Oversized or ambiguous requests fail with status 2 rather than silently dropping
buffers. Failed requests do not write reflection output; successful reflection
export cannot overwrite a supplied source, even an unused one.

Workspace analysis also bounds consumed source inputs, including disk imports,
textual includes and embedded library sources, to 256 sources and 8 MiB in
total, with 2 MiB per source. Textual preprocessing allows 32 include levels,
1,024 visits per source unit and 8 MiB of expanded text; macro substitution is
bounded during expansion.
These source-provider limits also apply when `--semantic-index` is requested,
including disk and raw-stdin analysis, so every consumed native source can be
captured consistently. Ordinary compilation and analysis without either option
retain their existing behavior.

Overlays participate at each normal source candidate, preserving import-root,
extension and source-before-binary search order. Only reachable files are
parsed; providing an unrelated buffer does not add its declarations to the
program. Ny textual includes use the same source provider, while their expanded
locations remain unavailable until an include source map exists. Synthetic
binary-library source identities cannot be replaced by workspace buffers.

Results still use `nil.analysis.v1`. This is one immutable input snapshot for a
check, not a persistent compiler server. The optional semantic index below
records supported bindings from that check. Sources
without overlays continue to come from disk. CLI project builds use saved files.
An open buffer remains authoritative if its file is deleted on disk. Removing
that buffer from the request restores normal missing-file resolution.

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

## Semantic source index

`--analyze --semantic-index` adds a `semantic` member to the analysis report.
The flag works with disk input, `--source-stdin`, and `--workspace-stdin`.
Without the flag, the member is absent. A successful supported check returns
`nil.semantic.v1`; an unavailable index is `null`.

```json
{
  "schema": "nil.semantic.v1",
  "partial": true,
  "sources": [
    {
      "path": "/project/main.ny",
      "text": "fn main() -> i32\n let value = 7\n return value\nend fn\n"
    }
  ],
  "symbols": [
    {"id": 0, "kind": "function", "name": "main", "type": "i32", "path": "/project/main.ny", "start": 3, "length": 4},
    {"id": 1, "kind": "variable", "name": "value", "type": "i32", "path": "/project/main.ny", "start": 22, "length": 5}
  ],
  "references": [
    {"symbol_id": 1, "path": "/project/main.ny", "start": 40, "length": 5}
  ]
}
```

All paths in this object, including the primary source, use canonical native
identities. This differs from the diagnostic envelope's logical primary path.
`sources` contains the exact text of **every consumed native input**, including
the primary buffer and textual includes with other extensions. Consumers must
validate the full set before using a binding: an unchanged destination can
resolve differently after another dependency changes. These snapshots are
independent of the smaller diagnostic `sources` array described above.

Each symbol has a nonnegative numeric `id`, a `kind` (`function`, `variable`, or
`parameter`), its compiler `name`, a display `type`, and a declaration name
location. `type` is a variable/parameter type or a function's return type, and
can be empty; it is not a stable type graph or complete callable signature.
IDs belong to this snapshot only. Each reference names its resolved
`symbol_id` and occurrence location. `start` and `length` are nonempty UTF-8
byte ranges in the corresponding exact source text.

The checker records these links when it resolves a symbol, before lexical
scopes are discarded. Initial coverage includes local/global variables,
function and lambda parameters, loop binders, variable reads and assignments,
and direct source-function calls across Ny and Matrix imports. Qualified
function references are included where the resolver retains the exact
declaration. Repeated visits that disagree about an occurrence's binding do
not emit a link. A matching name alone never creates one.

Both frontends retain original name-token ranges. Matrix's surface spelling
can differ from its compiler name through identifier folding. Ny retains
original ranges when preprocessing is unchanged, or differs only by verified
blank-line/final-newline normalization. Other textual transformations suppress
the unit's name spans. Generated/interpolated nodes without an original name
token remain unlocated. There are no guessed offsets into expanded text.

`partial: true` is deliberate. Field/member dispatch, type-use strings,
pattern bindings, full generic/overload occurrence coverage, failed-code
recovery, and source maps for expansion/generated code remain incomplete.
An empty reference list does not prove that a symbol is unused. This version
supports definition navigation for emitted bindings; it is not a complete
Find All References or rename contract.

The index is available only when the full analysis succeeds, including rule
policy. Parser, import, type, ownership, and warnings-as-errors failures return
`semantic: null`. Any consumed binary library also makes the index unavailable
until the protocol can validate binary dependency identities and source maps.
A source whose canonical identity changes during export invalidates the whole
index.

Limits are 256 source snapshots, 32,768 symbols, 131,072 references, 4 MiB of
encoded semantic JSON, and a 4 MiB checker metadata-string budget. Exceeding
an index limit returns `semantic: null`; symbols or inputs are not silently
truncated to create a smaller graph. The index is also withheld if adding it
would exceed the full analysis response's 8 MiB bound. Existing input/provider
limits still produce an analysis error as described above. Source text and
span validation may omit unsupported individual locations within an otherwise
valid partial index.

See [NIL-0010](design/NIL-0010-semantic-source-index.md) for the design decision
and [developer tooling](developer-tooling.md) for the remaining coverage.

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
own Problems collection. Live checks capture the primary buffer, all dirty named
source buffers and clean sources with open text or diff tabs in one workspace
request. Clean cached documents whose tabs have closed are excluded. Imported
diagnostics appear in Problems only when their bounded source snapshot matches
the captured editor buffer or, for unopened files, current disk text; remaining
imported/unlocated records go to the analysis
output channel. Any input change invalidates the check, including dependency
edits, document open/close events and changes to source tabs. It cancels superseded
work, debounces edits, limits process time/output, validates byte boundaries,
and applies fixes only to the same document version that produced them.
Compiler `extraArgs` are deliberately
not forwarded to analysis. Existing structural LSP diagnostics remain separate.

Within a `nil.project.json` project, live checks inherit project import roots,
SDK location and rule policy. Project build/run tasks use `nilbuild` and saved
files. The current-file command keeps the active file as its primary source;
other open source buffers supply dependencies. Live analysis requests the
semantic index and uses its resolved links for Go to Definition. Before
navigation it checks input versions, canonical identities, and every exported
source against its captured buffer or current bounded disk read. It also
observes open aliases and rejects changed models during asynchronous checks.
With compiler analysis enabled, the extension suppresses the language server's
older definition guesses. Disabling compiler analysis retains that fallback.

An untitled buffer with an explicit absolute source filename can participate
as an unsaved import. An unnamed untitled buffer has no import identity and is
included only when it is the primary file. Conflicting open aliases and input
limits produce an analysis error rather than selecting one buffer silently.
Only supported Ny/Matrix source filenames participate in overlays. A successful
semantic index adds file watchers for all exported native inputs, including
other textual include extensions. These includes remain disk inputs; unsaved
include edits cannot be substituted by the overlay protocol, and navigation
is withheld while a matching open include model differs from disk. A failed or
unavailable index cannot discover new dependency watchers. No navigation is
published from stale, failed, oversized, or otherwise unverifiable bindings.

Use a current SDK/compiler. The extension prefers the repository's `sdk` tools;
a stale SDK can otherwise mask a newer build-tree compiler.
