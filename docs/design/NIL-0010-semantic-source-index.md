# NIL-0010: Compiler bindings for source navigation

- **Status:** Implemented initial partial source index and definition navigation
- **Scope:** Shared compiler analysis and editor semantic services
- **Date:** 2026-10-19

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
through verified blank-line/final-newline normalization; other textual
transformations leave the unit unlocated. Matrix preserves written-token spans
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

## Consequences

Definition navigation can follow proven bindings across Ny and Matrix source
files without another editor-side interpretation of language semantics. The
compiler still runs once per analysis request; this is not a persistent
workspace server or a complete semantic editing API.

The `type` field is display metadata: a variable/parameter type or function
return type, potentially empty. It is not a stable type graph or complete
signature. Fields, method dispatch, type-use strings, patterns, complete generic
and overload coverage, recoverable failed code, and generated-source maps need
further work. Missing references cannot prove that a declaration is unused.
Find All References and rename must wait for coverage and explicit completeness
contracts; Matrix rename also needs surface-spelling rules.

The [analysis API](../analysis-api.md) defines the schema and failure behavior.
The [developer-tooling plan](../developer-tooling.md) records the remaining
work, and [NIL-0009](NIL-0009-workspace-analysis.md) defines workspace inputs.
