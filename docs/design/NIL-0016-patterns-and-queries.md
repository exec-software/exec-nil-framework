# NIL-0016: Shared text patterns and collected queries

Status: implemented initial byte-pattern and eager array-query profiles.

Ny and Matrix express the same operations through a shared AST, NIL, and
runtime. Text patterns need readable structure without giving up familiar regex
notation. Queries need composition without requiring escaping closures or
pretending a local array is a database provider.

## Text patterns

A named prose pattern lowers to an immutable **text source constant**. The
compiler validates that source using the runtime engine. `compile_pattern`
creates the public `Pattern` value used by `nil.std.text`; runtime construction
validates again. This is intentionally a source declaration, not a distinct
first-class language type or an owned native handle.

Both conventional notation and prose use the NIL byte-pattern profile. The
shared engine lives in `nilcompiler/src/pattern_engine.cpp`, links into the VM
and native runtime, and bounds parsing, compilation, backtracking and cached
program storage. No host regex package is required. The engine is a bounded
backtracking interpreter; it does not claim linear-time execution. Reaching a
budget returns an error, never a false no-match result.

Prose has a closed grammar: literals, byte classes/ranges, sequence, ordered
alternatives, repetition, captures, input anchors and zero-width lookahead.
Literals are escaped and do not interpolate. Named declarations use ordinary
symbol visibility and source spans. Named capture labels belong to the pattern,
not the compiler's variable namespace. Matrix lowercases ASCII capture labels
and translates hyphens to underscores; the runtime's capture-name lookup remains
case-sensitive. Conventional regex names are preserved exactly.

[The pattern contract](../standard-library/text-patterns.md) defines syntax,
limits, captures and iteration. Unsupported dialect features fail explicitly;
PCRE, ECMAScript, POSIX and RE2 compatibility are not claimed.

## Collected queries

`collect query ... end query` in Ny and `collect begin query ... end query` in
Matrix lower to ordinary NIL loops and array operations. A shared `ast_query_expr`
keeps the binding, source, predicate, ordering, pagination and projection until
type checking. The checked profile accepts arrays of scalar values, text, and
supported structural tuples, including nested tuples of those values.

The result is an eagerly allocated array. Source, skip and take expressions run
once in that order, in the enclosing scope. Negative counts fail before row
processing. Source elements are then copied into a separate shallow snapshot.
The query binder is immutable and local to row expressions. It does not escape.

Without ordering, filtering and pagination walk the snapshot in input order and
stop after the requested number of selected rows. With ordering, all qualifying
rows receive a single key evaluation and stable merge sorting precedes pagination.
Projection runs once per selected row after pagination. This avoids duplicated
side effects and gives equal-key records a deterministic order. The standard
library adds terminals over the resulting array.

[The query contract](../standard-library/queries.md) defines exact syntax and
execution order. A query is not a deferred iterator, SQL translation, a captured
closure or an implicit conversion from a database connection. Future grouping,
joins, multiple ordering keys and provider translation require separate contracts.

## Qualification

Focused parser/engine contracts, paired Ny/Matrix fixtures, serialized NIL,
interpreter/JIT/AOT routes, imported library consumers and editor grammar tests
cover the implemented profile. These tests qualify the listed operations, not
all legacy experimental LINQ or regex sources. Those sources remain excluded
from the SDK.
