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
The example shows the navigation fields; additive local-editing fields are
described below.

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
blank-line/final-newline normalization or the removal of standalone `@pragma once`
and `@schema` metadata lines. These directives must become exactly one empty
line while every code line remains byte-for-byte unchanged; an explicit line
offset map then restores original UTF-8 positions. Other textual transformations suppress
the unit's name spans. Generated/interpolated nodes without an original name
token remain unlocated. There are no guessed offsets into expanded text.

`partial: true` is deliberate. Field/member dispatch, type-use strings,
pattern bindings, full generic/overload occurrence coverage, failed-code
recovery, and source maps for expansion/generated code remain incomplete.
An empty reference list does not prove that a symbol is unused unless that
individual symbol carries the completeness capability described below.

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

### Certified local references and rename

An eligible symbol additionally has `references_complete: true`; a candidate
for rename also has `rename_safe: true`. Missing flags mean unsupported; consumers must never
infer either capability from `kind`, the number of references, or successful
checking. The whole index remains partial. These flags currently apply only
to variable declarations inside function/lambda/test bodies and explicit loop
binders, with all occurrences confined to their declaration's source file.
Global variables, functions, fields, types, pattern bindings, and all parameters
remain outside this editing profile. Named function parameter names can be
used as Matrix argument labels by callers outside the analyzed graph.

The checker retains actual binding IDs and records missing original spans.
Export then lexes the original source and requires every identifier token with
the declaration's key to have an unambiguous retained declaration or reference.
This includes other declarations with the same spelling: their separate IDs
preserve shadowing. An unsupported field/type/argument-label occurrence with
that key withholds the capability rather than guessing its role. The supported
symbol's own references must all be original name tokens in the same file.
Ny sources containing an ordinary string with `{` are excluded because legacy
implicit interpolation is not mapped. Explicit `$"..."` and Matrix
`formatted "..."` expressions retain original name tokens and participate in
the normal coverage proof, even after decoded literal prefixes. Assembly in
either language also excludes the source. Preprocessed or generated units
without original spans cannot qualify.

Matrix named arguments now follow the checked callable's parameter-name contract.
A certified local may therefore be renamed even when it shadows a paragraph name;
argument order no longer depends on a parser-wide signature lookup. Function
parameters remain ineligible because callers can use their names as labels.

The index includes an additive `rename_policy` object with
`schema: "nil.rename.v1"` and a `sources` array. Each entry contains:

- `path`: the exact canonical source path already present in `sources`.
- `language`: `"ny"` or `"matrix"`.
- `identifier_keys`: the unique original identifier keys throughout that file.
- `reserved_keys`: language keywords and reserved compiler operation names.

Ny keys preserve spelling. Matrix keys fold ASCII case and remove hyphens and
underscores. Policy entries exist only for sources with a certified rename candidate.
There are at most 32,768 identifier keys per source, 131,072 in total, and
131,072 name tokens examined per source for certification. A coverage-budget
failure withholds that source's editing capabilities while retaining ordinary
navigation; the complete JSON still shares the 4 MiB semantic-output bound.

`rename_safe` authorizes a candidate for validation, not an arbitrary
replacement. Clients must require a policy for the source, accept a bare
identifier in the declared language, and reject empty normalized keys,
reserved keys, and keys already present anywhere in that source. The current
VS Code profile accepts ASCII identifiers: letters/underscore followed by
letters/digits/underscore, with Matrix additionally allowing hyphens followed
by a letter. Existing backticks stay outside the edit ranges. Every edited
occurrence receives the same new spelling.

Before offering edits, a client must analyze the proposed replacement against
the same complete workspace snapshot and require successful analysis with a
semantic index. Compare the entire indexed binding graph after accounting for
changed byte offsets and the renamed declaration; preserve all other resolved
identities. Snapshot-local numeric IDs cannot be compared directly across
checks. This second check catches contextual grammar changes and unexpected
rebinding. Revalidate original source versions and canonical identities after
that asynchronous check, before returning an edit. A failed or unverifiable
proposal returns no edit. References use the same snapshot validation and may
include the declaration according to the caller's requested context.

This profile does not add workspace-wide references for exported symbols,
parameter rename or lexical scope ranges. Completion uses the separate query
described below. Older `nil.semantic.v1` producers without the new fields continue to
support navigation, with local references/rename unavailable.

See [NIL-0010](design/NIL-0010-semantic-source-index.md) for the design decision
and [developer tooling](developer-tooling.md) for the remaining coverage.

## Completion at an identifier

`--completion-at <offset>` adds a `completion` member to `nil.analysis.v1`.
The offset is the cursor's UTF-8 byte offset in the original primary source.
The flag works independently of `--semantic-index`, with disk, `--source-stdin`,
and `--workspace-stdin` inputs:

```sh
sdk/nilcompiler --analyze --workspace-stdin --completion-at 82 \
  -I sdk/nilstd application.matrix < workspace.json
```

The query accepts a nonempty prefix inside an unqualified value identifier or
a supported receiver member name, including direct-call and function-value
expressions.
Both frontends must have parsed successfully, imports must resolve, and the
checker must reach the identifier without an earlier checker diagnostic. At
that point it snapshots the actual visible bindings before looking up the
unfinished name. A subsequent undefined-name diagnostic does not invalidate
the snapshot. Consequently a valid completion can accompany `success: false`
and exit status `1`; clients must inspect the query member independently of
whole-program success. Exit status `2` still indicates an invalid request.

An available result has this shape:

```text
completion: {
  schema: "nil.completion.v1",
  partial: true,
  path: canonical_primary_path,
  cursor: requested_byte_offset,
  start: replacement_start_byte,
  length: replacement_length_bytes,
  sources: [{ path: canonical_native_path, text: exact_consumed_source }],
  items: [{ name: display_spelling, kind: "function" | "variable" | "parameter" | "field" | "method",
            type: display_type, insert_text: complete_identifier_spelling }]
}
```

Items come from lexical lookup and the current module's imports, including
visibility checks, shadowing and declaration order. Source functions declared
later in the module remain available according to normal function lookup.
Candidates require a verified original declaration token. Member queries use
the checked receiver type to select source-backed fields and instance methods,
including imports, inheritance and supported generic substitutions. Accessibility
and ambiguous member lookup use the same rules as checking. Type names, static
member contexts, unlocated intrinsics and unsupported bindings are omitted. `type` is
display metadata, using Matrix vocabulary for a Matrix query; function entries
and method entries expose their return type rather than a complete signature.
Source-backed library declarations behind `@pragma once` and `@schema` headers
retain those tokens through the verified metadata-line mapping above. Includes,
macro expansion and conditional rewriting still require a general source map.

The compiler filters by the prefix ending at the requested cursor. Ny matching
is case-sensitive. Matrix matching uses its ASCII case/separator folding, and
the insertion is checked against the same page-wide name resolver used by the
frontend. A spelling redirected to another declaration is omitted. Member queries instead
validate receiver-directed spelling and do not use the page-wide resolver. Matrix
declarations retain their written display spelling; Ny declarations retain
their Ny spelling. An edit replaces the entire current identifier, including
any suffix after the cursor and existing Matrix backtick delimiters. Required
backticks are included in `insert_text`.

An empty item array is an available query with no matching supported bindings.
`completion: null` means the position or snapshot is unavailable. Positions in
comments, literals, type names, unsupported members, qualified names, and blank input
are not recovered into value queries. Earlier checker errors, parse/import
failure, preprocessed names without original spans, binary-library dependencies,
unstable source identities, repeated checker visits to the queried physical
token (for example through generic specialization), and exhausted budgets also produce `null`. Omitting
the flag omits the member entirely. This protocol does not claim that a proposed
value meets the surrounding expression's expected type or call signature.

Every consumed native source, including non-language textual includes, is
present in `sources`; the existing workspace overlay restrictions still apply.
Clients must revalidate all captured versions, canonical identities, aliases,
and source text before using a result. Limits are 256 source snapshots, 4,096
exported candidates, 4 MiB of encoded query JSON, and the existing 8 MiB total
analysis response. A query that exceeds a bound becomes `null`, without
truncating its candidates or input graph and without displacing diagnostics.

The design rationale is recorded in
[NIL-0011](design/NIL-0011-completion-queries.md).

## Signature help at a call

`--signature-at <offset>` adds an independent `signature` member to
`nil.analysis.v1`, using the same original UTF-8 cursor offsets and disk,
raw-stdin or workspace input routes as completion. The compiler selects the
innermost parsed call argument region at the cursor and resolves its actual
callable. An unavailable inner call does not fall back to an unrelated outer
signature. Parameter information is separate from the semantic reference index
and does not certify parameters or argument labels for rename.

```text
signature: {
  schema: "nil.signature.v1",
  partial: true,
  path: canonical_primary_path,
  cursor: requested_byte_offset,
  start: argument_region_start_byte,
  length: argument_region_length_bytes,
  sources: [{ path: canonical_native_path, text: exact_consumed_source }],
  label: full_display_signature,
  parameters: [{ name, type, label: display_parameter, variadic: false }],
  active_parameter: zero_based_index_or_null,
  return_type: display_return_type
}
```

Argument regions exclude the Ny call parentheses and start after Matrix's
`with`. A zero-length region is valid for an empty Ny call. Original argument
slots and named labels survive checker reordering. Matrix's active parameter
follows the selected callable's label, including folded spellings, rather than
the written argument index. A variadic tail selects its final parameter.
Unknown labels or positions without a corresponding parameter use null.

Source-backed direct and imported functions, supported methods, and immutable
callbacks with a proven parameter-name contract can provide signatures. Instance
methods omit `self`. Display types describe the selected declaration rather than
speculatively inferring missing generic arguments. Invalid or unresolved argument
expressions may still yield useful help; an earlier unrelated checker failure,
unresolved callee, inaccessible callable or unsupported callback contract does not.

The initial query requires parsed call syntax, with one narrow Matrix exception:
while querying whitespace immediately after a bare `with` on that line, the parser
can retain an empty argument region. The original source remains a syntax error,
ordinary compilation still rejects it, and analysis reports `success: false` with
no semantic index. This query recovery never alters the consumed source snapshots.
Missing Ny closing parentheses and other parse errors remain unsupported.

A result is limited to 256 parameters, 256 consumed native sources and 4 MiB of
JSON within the existing 8 MiB analysis response. Binary dependencies, unverifiable
source identities/spans, ambiguous repeated call occurrences and exceeded limits
produce `signature: null`. Omitting the flag omits the field. Clients must validate
all input snapshots and cursor bounds before displaying the result; queries do not
publish diagnostics or make edits. See [NIL-0012](design/NIL-0012-signature-queries.md).

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

Completion queries run on demand for the active identifier and share the same
captured-buffer and dependency validation. A verified compiler result supplies
value candidates while the language server can retain keyword suggestions;
unavailable queries retain the structural completion fallback. Query snapshots
also establish watchers for all consumed native inputs.

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
