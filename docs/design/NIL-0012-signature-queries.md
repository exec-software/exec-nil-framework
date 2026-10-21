# NIL-0012: Signature queries at resolved calls

- **Status:** Implemented initial source-backed call signatures
- **Scope:** Shared compiler analysis and Ny/Matrix editor signature help
- **Date:** 2026-10-20

## Context

An editor needs the callable selected by lexical scope, imports, receiver type,
and visibility. A document-wide list cannot distinguish a shadowing callback
from a same-named function. Matrix also permits argument labels in a different
order from declaration parameters; counting commas is not enough to choose the
active parameter.

## Decision

Add the independent `--signature-at <UTF-8-byte-offset>` analysis query. An
available response is an additive `signature` object with schema
`nil.signature.v1`, `partial: true`, the canonical primary path, requested
cursor, argument-region `start` and `length`, exact consumed native `sources`,
a display `label`, `return_type`, `parameters`, and nullable `active_parameter`.
Each parameter has `name`, `type`, display `label`, and `variadic` fields. The
query flag is required; unsupported queries return `signature: null`.

Both parsers retain the native argument region and source-order slot boundaries
on call expressions. Ny's region lies after `(` and before `)`; an empty call
has zero length. Matrix's region begins after `with` and ends with its last
argument token. Cursor bounds are inclusive. A slot begins immediately after
its preceding delimiter, so whitespace after a comma belongs to the next
argument. Nested expressions retain their own delimiters. Matrix argument
labels remain in source order independently of the checker's argument reorder.

The checker selects the innermost parsed call containing the cursor before
resolving its callee. An unresolved inner call therefore suppresses an outer
call's signature. Signature metadata comes from the actual selected source
function or method; implicit `self` is excluded from instance-call parameters.
Immutable local callbacks may retain the checker-proven parameter-name
contract, while their parameter types come from their structural function type.
Mutable callbacks, unnamed callback parameters, and heuristic builtins do not
have the required source/label proof and return no query.

Named Matrix arguments select their matching parameter using the same ASCII
case and separator folding as call checking. Unknown or ambiguous labels yield
no active parameter. Positional arguments use their written slot, with variadic
tails selecting the last parameter. Excess nonvariadic arguments have no
active parameter. The signature remains useful when the argument count or an
argument expression is invalid. Generic parameter types describe the declared
signature; the query does not invent a specialization from unfinished inputs.

Ny and Matrix types are rendered in the caller's vocabulary. Matrix declaration
spellings are retained for Matrix display where their original parameter spans
are available. Callback parameter names may use their canonical underscore
spelling, because immutable callable provenance retains names but not the
original declaration's editable spans.

The parser admits one narrow query-only recovery: Matrix `with` followed by the
cursor in same-line spaces or tabs, before a token on a later line or EOF. It
creates an empty argument region for signature resolution without altering the
snapshot. Analysis still reports `MX0001`, `success: false`, and
`semantic_checked: false`; normal compilation rejects the same unfinished
source. Other parse failures remain unavailable. Ny's balanced empty `()` needs
no recovery.

Earlier checker diagnostics, inaccessible callees, unavailable original spans,
parse/import failures, binary dependencies, repeated physical calls under
specialization, and unstable source identities withhold the query. The response
contains at most 256 parameters, 256 source snapshots, and 4 MiB of query JSON,
within the existing 8 MiB analysis response. No input graph is silently
truncated to fit. Exact native snapshots follow
[NIL-0009](NIL-0009-workspace-analysis.md), independently of the successful
semantic index used for editing.

## Consequences

VS Code can display call help and select Matrix parameters by their actual
labels while applying the same document/dependency freshness checks as
completion and navigation. A signature query can succeed independently of a
failed normal analysis. It does not certify references or rename safety.

This profile does not provide overload sets, inferred generic specializations,
parameter documentation, arbitrary call-expression recovery, dynamic callable
provenance, or fabricated runtime builtin signatures. Unsupported contexts
currently have no signature help. Those capabilities require explicit contracts
rather than broadening the meaning of a successful response.

The [analysis API](../analysis-api.md) defines the wire fields. The signature
contract tests exercise both frontends, all input routes, UTF-8 offsets,
source-order and nested arguments, callback shadowing, methods, imported
visibility, incomplete Matrix calls, and invalid requests.
