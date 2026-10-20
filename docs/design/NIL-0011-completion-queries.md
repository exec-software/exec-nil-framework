# NIL-0011: Completion queries at checked lexical scopes

- **Status:** Implemented initial value-identifier query
- **Scope:** Shared compiler analysis and Ny/Matrix editor completion
- **Date:** 2026-10-20

## Context

The semantic index introduced in [NIL-0010](NIL-0010-semantic-source-index.md)
requires a successful analysis. It supports navigation and certified local
editing, but an unfinished identifier normally causes checking to fail. A
document-wide declaration list also cannot establish lexical visibility,
shadowing, declaration order, or imported symbol visibility at a cursor.

## Decision

Add an independent, opt-in `--completion-at <UTF-8-byte-offset>` query to the
existing analysis command. It targets an original unqualified identifier
expression in the primary source. The compiler reads and parses the complete
source graph normally. Immediately before the checker resolves that identifier,
it captures the visible value bindings using the current symbol table and
module imports. Earlier checker diagnostics make the query unavailable; a
diagnostic caused by the unfinished identifier or later code does not invalidate
the already captured scope.

Export the result as the additive `completion` member of `nil.analysis.v1`.
An available `nil.completion.v1` object contains `partial: true`, the canonical
primary path, requested cursor offset, whole-token replacement span, exact
consumed native source snapshots, and bounded matching items. Each item has a
display name, value kind, display type, and complete insertion spelling. The
query may be available when ordinary analysis returns `success: false` and exit
status `1`. No query flag means no new member; an unsupported query returns
`completion: null`.

Candidates are actual source-backed functions, variables and parameters. The
compiler filters the prefix using Ny's case-sensitive spelling or Matrix's
case/separator folding. Matrix insertion spellings pass the frontend's actual
page-wide resolver so an unrelated folded declaration cannot silently redirect
the insertion. Required backticks belong to the insertion, and replacement
ranges include existing delimiters. These edits replace the whole current
identifier, including a suffix after the cursor.

The initial profile deliberately does not recover missing syntax. Blank
positions, type/member/qualified-name positions, literals and comments produce
no value query. Parse/import failures, earlier checker diagnostics, unavailable
original spans, binary dependencies, unstable source identities, and exceeded
budgets produce `null`. Revisiting the same physical token during generic
specialization also withholds the query rather than choosing one inferred
scope. Unsupported bindings are omitted. An available empty
array has the distinct meaning that no supported binding matches the prefix.

All native dependencies participate in snapshot validation, including textual
includes outside the language extensions. Limits remain bounded: 256 input
snapshots, 4,096 exported items, 4 MiB query JSON, and the existing 8 MiB analysis
response. Overflow withholds the optional query without truncating its source
graph or displacing diagnostics. Source snapshots use the established
[NIL-0009](NIL-0009-workspace-analysis.md) overlay contract.

## Consequences

Completion works while a value name is unfinished, using the same actual scope
and imports as the compiler. It does not require publishing incomplete
navigation or editing bindings. VS Code can request the query on demand,
coalesce duplicate requests for the same source snapshot and cursor, and use
the same freshness checks as semantic navigation. Existing structural language
server completion remains available when the compiler query is unavailable.

This is lexical value completion, not expected-type filtering, member lookup,
signature help, import insertion, a persistent workspace index, or general
parser recovery. A suggested binding may still require arguments or a different
surrounding expression to type-check. Broader completion and project-wide
rename need separate coverage and identity contracts.

The [analysis API](../analysis-api.md) specifies the request and result fields.
The compiler contract tests exercise unresolved prefixes, lexical boundaries,
mixed-language imports, Matrix spellings, replacement ranges, failure cases,
input routes, and independent query budgets.
