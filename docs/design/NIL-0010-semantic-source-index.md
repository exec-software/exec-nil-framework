# NIL-0010: Compiler bindings for source navigation

- **Status:** Implemented partial source index, definition navigation, and certified local editing
- **Scope:** Shared compiler analysis and editor semantic services
- **Date:** 2026-10-19
- **Updated:** 2026-10-20

## Context

Workspace overlays establish consistent source inputs, but declaration names
and inferred type strings do not identify which lexical or imported symbol an
expression resolved to. A document-local language-server index cannot safely
choose among shadowed locals or functions in multiple modules. Precise editing
also requires locations in the original source rather than reconstructed or
preprocessed text.

## Decision

Both frontends retain optional UTF-8 byte spans for original name tokens in the
shared AST. The type checker collects declaration IDs and occurrence links at
actual symbol-resolution points. Parameter and local identities survive scope
exit, and copied symbol metadata retains the same declaration identity.

`nilcompiler --analyze --semantic-index` exposes those records as the additive
`semantic` field in `nil.analysis.v1`. Its `nil.semantic.v1` object contains
`partial: true`, exact consumed native `sources`, `symbols`, and `references`.
IDs are local to one snapshot. Paths, including the primary path, are canonical
native identities. Source snapshots include textual includes of other
extensions, although workspace overlays remain limited to Ny/Matrix sources.
The flag works for workspace, raw-stdin, and disk analysis and enables the
bounded source provider for each route.

The first profile includes supported function/variable declarations,
parameters and loop binders, and their resolved reads, assignments, and direct
calls. It emits only validated original-token locations. Ny may retain spans
through verified blank-line/final-newline normalization and standalone
`@pragma once`/`@schema` lines that become exactly one empty line. Every code
line must remain byte-for-byte unchanged, and the mapper restores its original
UTF-8 offset rather than reporting an offset into the shortened text. Other
textual transformations, including includes, macros and conditional rewriting,
leave the unit unlocated. Matrix preserves written-token spans
while the checker uses its resolved compiler spelling. Conflicting bindings
for a repeated physical occurrence are omitted.

The full analysis must succeed, including rule policy. Failure, unsupported
binary dependencies, unstable canonical identities, or an index limit produce
`semantic: null`. Binary libraries require dependency identity and source-map
support before their influence can be validated by a client. The index is
bounded to 256 source snapshots, 32,768 symbols, 131,072 references, 4 MiB of
encoded JSON and 4 MiB of checker metadata strings. Optional index output must
also fit the existing 8 MiB analysis response limit. It cannot displace valid
diagnostics or silently truncate the input graph.

VS Code requests the index for live checks and supplies Go to Definition from
its resolved links. It validates captured source versions, aliases, canonical
identities, and every exported disk snapshot before returning a location. Open
textual include models that differ from their disk input invalidate navigation,
including aliases and changes during asynchronous validation. Successful indices
add watchers for all exported native dependencies. While compiler analysis is
enabled, the older language-server definition provider is suppressed;
disabling analysis retains its fallback behavior.

The initial editing profile adds per-symbol `references_complete: true` and,
for eligible rename candidates, `rename_safe: true`, without changing
`partial: true` or the schema version.
Only variable declarations proven to occur in a checked body and explicit
loop binders are candidates. Export verifies their physical name occurrences
against every original identifier token sharing the declaration's key. Every
such token must already be a retained declaration or unambiguous actual
reference; unsupported occurrences disqualify the spelling rather than
acquiring an inferred role. Each candidate's references must stay within its
original source and have exact name tokens. Missing-span checker uses also
disqualify a candidate. Globals, functions, parameters, fields, types, and
pattern bindings remain unsupported. In particular, named function parameters
can be used as argument labels by callers beyond the analyzed source graph.

Ny interpolation lacks original name spans, so a Ny string containing `{`
excludes that entire source from editing. Assembly in either language does the
same: source checking does not enforce the eventual native assembler profile.
These conservative exclusions retain definition navigation where available.

Complete references alone do not authorize rename. Matrix formerly reordered
named arguments through a parser-wide signature table before lexical resolution,
so renaming a shadowing local could change argument order. Named arguments now
follow the checked callable, including proven immutable function values. Runtime
regressions establish equivalent results before and after rename, and the prior
exclusion for shadowing locals is removed. See F43 in the findings document.

An additive `rename_policy` (`nil.rename.v1`) exports the language and unique
identifier/reserved keys for each source with a certified local. Matrix keys
use its ASCII case/separator folding. Coverage is bounded to 32,768 identifier
keys per source, 131,072 keys in total, and 131,072 name tokens per source.
Exhausting these coverage bounds withholds editing for the source. The policy
still counts toward the existing semantic JSON limit; no truncated policy can
authorize an edit. Older producers lacking capabilities remain usable for
definition navigation only.

VS Code's local rename requires an ASCII bare identifier, excludes all reserved
and already-present source keys, preserves existing backtick delimiters, and
edits only the certified declaration plus references carrying its ID. Before
returning the edit, it checks the proposed source with the compiler, compares
the entire indexed binding graph with translated byte locations and the one
renamed declaration, and revalidates the original workspace snapshot. This
prevents contextual syntax changes or binding capture from being accepted just
because the new source type-checks. Numeric IDs belong to a single snapshot
and are matched through source declaration identities across checks.

## Consequences

Definition navigation can follow proven bindings across Ny and Matrix source
files without another editor-side interpretation of language semantics. The
compiler still runs once per analysis request; this is not a persistent
workspace server or a complete semantic editing API.

The `type` field is display metadata: a variable/parameter type or function
return type, potentially empty. It is not a stable type graph or complete
signature. Fields, method dispatch, type-use strings, patterns, complete generic
and overload coverage, recoverable failed code, and generated-source maps need
further work. Missing references cannot prove that a declaration is unused
without its explicit completeness capability. Local references/rename are now
available within that narrowly certified profile; workspace-wide exported
symbol editing, parameter editing and lexical visibility ranges still require
broader contracts. [NIL-0011](NIL-0011-completion-queries.md) supplies a separate
initial identifier-completion query, including unresolved prefixes.

The [analysis API](../analysis-api.md) defines the schema and failure behavior.
The [developer-tooling plan](../developer-tooling.md) records the remaining
work, and [NIL-0009](NIL-0009-workspace-analysis.md) defines workspace inputs.
