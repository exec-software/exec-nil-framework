# NIL-0014: Explicit formatted text with original expression locations

- **Status:** Implemented initial scalar/text profile
- **Scope:** Ny and Matrix syntax, shared checking and lowering, native/VM conversion, editor source locations
- **Date:** 2026-10-20

## Context

Ny's legacy ordinary-string interpolation recognizes simple identifiers but
does not parse general expressions. It constructs identifier nodes from
decoded text, which loses editable original positions. Numeric interpolation
previously emitted an operation unsupported by the native backend; the VM used
six fractional digits for floating-point values. Matrix ordinary quoted text
and `begin text` blocks are deliberately raw, so implicit substitutions would
change existing source meaning.

## Decision

Introduce explicit interpolation in both surfaces:

```ny
let message = $"Count: {count + 1}; offset: {offset}"
```

```text
value message is formatted "Count: {count plus 1}; offset: {offset}"
```

`formatted` is contextual only when followed by a quote on the same source
line. It remains a usable Matrix identifier elsewhere. Ordinary Matrix quoted
strings and raw blocks do not change. Existing simple implicit Ny interpolation
remains accepted; ordinary `"{count + 1}"` is still literal text.

Each hole contains an ordinary expression in its frontend's grammar. Calls,
member access, arithmetic, conditional expressions and nested quoted strings
use their existing meanings. The whole explicit formatted string, including
all holes, occupies one physical source line. Multiline formatted blocks are
outside this profile; Matrix's named `newline` value or Ny's `\n` escape can
produce newlines in the resulting text.

`{{` and `}}` insert literal braces. Ny retains its ordinary backslash decoding
in literal segments. Matrix backslashes remain ordinary characters. Literal
decoding never applies to an embedded expression: quotes in a call such as
`$"{choose("one", "two")}"` are the expression's own string delimiters.
Malformed, empty and unfinished holes are diagnostics; there is no fallback
that silently turns an invalid explicit expression into literal text.

Expressions are evaluated exactly once, left to right, including their
conversions. The result is `text`. The initial supported hole types are `text`,
`bool`, `i32`, `i64`, `u8` and `f64`, including checked aliases of those types.
Other values must be converted explicitly by a library function or method.
No format flags or precision-specifier grammar is introduced.

A generic body may interpolate a declared type parameter. This records an
implicit scalar/text requirement on the actual function declaration. After
checking all declarations, the checker propagates requirements through resolved
generic call substitutions, including forwarding wrappers and generic receiver
methods. Unsupported concrete instantiations are diagnosed at their callsites
during ordinary checking and `--analyze`, even when the formatter is declared
later or imported. Code generation independently checks each specialization;
the editor does not report a successful check for a known unsupported aggregate
that will only fail during lowering.

## Shared conversion and NIL compatibility

The checker records each hole's resolved type. Lowering preserves the ordered
literal/expression parts, emits a typed `TO_STRING` for supported non-text
values and combines text parts with `STR_CONCAT`. Text values need no
conversion. An empty formatted string still produces an empty text value.

`TO_STRING` accepts zero or one operand describing its source type. Canonical
typed operands are `bool`, `i32`, `i64`, `u8`, `f64` and `text`. The existing
zero-operand instruction remains accepted for old NIL; new source lowering
supplies the resolved scalar type. A typed operand must agree with its actual
runtime representation and native metadata. Unsupported types are rejected
instead of manufacturing display text for arbitrary handles or records.

Both execution engines use the shared scalar-text implementation:

- Integers use ordinary decimal notation; booleans use `true` and `false`.
- Floating-point text uses a locale-independent shortest round-trip
  representation. The sign of zero is preserved as `-0`.
- Nonfinite runtime values use `nan`, `inf` and `-inf`.
- Text retains its contents within the supported text domain. Native text
  remains NUL-free; reachable embedded-NUL constants are rejected before native
  execution, including constants originating in formatted literal segments.

This also changes legacy implicit Ny floating interpolation from fixed six
fractional digits to the shared shortest representation, and makes that
numeric interpolation available on the native route. Callers requiring a
fixed decimal width must call an explicit precision formatter. This change
does not redefine the separate legacy scalar-to-text cast contract.

## Original source locations

The lexers emit explicit formatted boundaries and literal segments while
emitting each hole's tokens as ordinary expression tokens. Every such token
retains its original UTF-8 byte offset, length and line information. Nested
formatted strings keep their own boundaries. Parsing therefore uses the
existing expression parser and preserves its name spans, call argument
regions and named argument order without building a second synthetic source.

The original source scanner used by semantic editing sees those same hole
tokens. They participate in existing definition, completion, signature,
reference coverage and safe-local-rename rules; the feature does not widen
those profiles. Literal words and doubled-brace text never become reference
tokens. Ny preprocessor transforms still require the existing exact source
mapping proof. Legacy implicit Ny interpolation retains its conservative
editing exclusion because its identifiers still lack original spans.

## Validation and future work

Parser/source-span contracts cover escaped literal prefixes, raw Matrix
backslashes, Unicode, CRLF, nested quoted/formatted strings, malformed input
and references following the literal. Compiler query contracts exercise hole
completion, call signatures and actual local rename followed by rechecking,
while preserving identical words inside literal segments. Execution contracts
exercise scalar conversion and ordering across native and interpreted routes.

A future multiline form must preserve original expression coordinates through
indentation removal. Custom record formatting can build on the existing
`Display` contract after its dispatch is qualified across execution routes.
Locale-sensitive or fixed-precision presentation remains an explicit library
operation.
