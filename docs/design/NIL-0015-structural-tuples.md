# NIL-0015: Structural tuples

- **Status:** Implemented
- **Scope:** Ny and Matrix syntax, shared checked types, NIL, interpreter and native aggregates

## Decision

A tuple is a nonempty structural product type whose identity includes the
ordered canonical types of all positions. Ny spells types `(T, U)` and Matrix
spells them `tuple of (T, U)`. Both construct `(a, b)`; a trailing comma makes a
single-element literal distinct from grouping. Ny singleton types use `(T,)`.

Slots are immutable. Copying the tuple may share its internal storage because
no source operation can replace a slot. Elements retain their existing value
or reference semantics: a contained record handle still aliases the original
record. Tuple construction evaluates each expression once in written order.
Destructuring evaluates the source once and creates local bindings afterward.
A discarded `_` position still evaluates during construction.

A tuple literal can use the same checked scalar storage conversions as record
construction. Existing tuple values are invariant; reinterpreting
`tuple<i32,text>` as `tuple<i64,text>` does not perform a field-by-field copy and
is therefore rejected. Generic substitution resolves each structural element.

## Shared representation

Tuples reuse the typed NIL record schema and existing `NEW_STRUCT`,
`SET_FIELD` construction, and `GET_FIELD` projection instructions. The schema
name is the canonical `tuple<T1,T2,...>` spelling, without module qualification
or lossy identifier sanitization. Fields are the decimal positions `0`, `1`,
and so on, in order. Generated schemas carry `@nil_tuple` metadata. Nested
shapes are materialized recursively and duplicate shapes must agree exactly.

The existing text and binary NIL metadata carry these schemas and public
function signatures. There is no heterogeneous array layout, tag-erased
argument pack, or frontend-specific tuple ABI. Native field validation still
rejects element layouts it does not implement. This preserves the framework's
existing acyclic aggregate-handle representation and does not promise C struct
layout or a C multi-return calling convention.

Tuple match patterns recursively load typed fields. Tuple-array loop
unpacking uses the same projection when the element shape is known; legacy
array unpacking retains its separate interpreter behavior. Static positional
access must be within the shape's arity. Numeric member tokenization keeps
`value.0.1` distinct from decimal literals.

## Boundaries

- No empty tuple/unit value; existing Ny `()` remains the `void` type.
- No named tuple elements, tuple slot mutation, dynamic positional indexing,
  implicit tuple covariance, or automatic record-to-tuple conversion.
- Declaration destructuring binds flat local names; nested values can be
  destructured with another declaration. Module/global destructuring is not
  supported.
- Owned buffers and borrows cannot be tuple elements.
- Arrays of tuples execute only in the interpreter until native aggregate
  arrays receive their own qualified layout contract.
- Tuple patterns support bindings, wildcards, scalar literals and nested
  tuples. This does not expand nested enum-payload patterns.
- A tuple inside a public signature has its canonical structural shape, but
  native FFI compatibility must still be declared and qualified separately.

## Validation

`tests/language/tuples.ny` and `tests/language/tuples-matrix.matrix` exercise
returned values, generic substitution, scalar conversion, nested projection, singleton values, ordering,
reference identity, discarded positions and match forms through executable
text/binary NIL, bundles and LLVM routes. `tests/tools/tuple_contract.py` checks
invalid shapes, slot writes, huge/out-of-range positions and arity errors,
verifies cross-language consumers using source and binary libraries with the
original source files removed, and tests the interpreter/native tuple-array
boundary. Interpreter loop coverage includes named and literal arrays as well
as singleton tuples in both frontends.
