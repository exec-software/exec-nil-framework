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

## Tuple arrays

Native tuple arrays use a distinct managed element kind containing traced tuple
handles. Their exact `tuple<T1,T2,...>` metadata selects the annotated record
schema; names of nominal types inside the shape retain their case. Empty arrays
materialize the schema too. Construction, indexed replacement, append and the
generic array intrinsics validate the same element shape, including when source
type checking is disabled. A tuple handle cannot be substituted for a different
ordered shape. Fresh literals may apply checked element conversions at a
declared construction boundary; existing tuple values and arrays remain invariant.

The container is mutable while tuple slots remain immutable. Replacing an array
element leaves extracted tuple handles intact. Shared record/reference fields
retain identity through aliasing, copying, array growth and function returns.
Native tracing follows tuple handles and their transitive text/aggregate fields.

Ordinary and destructuring loops evaluate their array expression once. Literal,
named and function-returned arrays use indexed traversal of the selected array;
rebinding a source variable does not retarget the running loop. Bindings keep
their normal scope and control-flow behavior, including `break` and `continue`.

Tuple-array reads require a valid initialized element. Out-of-range reads fail
instead of manufacturing a tuple with default fields. Out-of-range writes remain
no-ops, and existing scalar-array bounds behavior is unchanged. This extension
does not enable native arrays with record, enum or array elements, nor arbitrary
pointer-valued array storage.

## Boundaries

- No empty tuple/unit value; existing Ny `()` remains the `void` type.
- No named tuple elements, tuple slot mutation, dynamic positional indexing,
  implicit tuple covariance, or automatic record-to-tuple conversion.
- Declaration destructuring binds flat local names; nested values can be
  destructured with another declaration. Module/global destructuring is not
  supported.
- Owned buffers and borrows cannot be tuple elements.
- Tuple arrays require one exact supported schema. Native arrays of records,
  enums and nested arrays remain outside this layout contract.
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
original source files removed. Its tuple-array loop cases cover direct, named
and returned arrays, singleton tuples, discarded positions, evaluation order
and control flow in both frontends and execution modes.

`tests/language/tuple-arrays.ny`, `tests/language/tuple-arrays-matrix.matrix` and
`tests/tools/tuple_array_contract.py` cover indexing, whole-tuple replacement,
scalar/text/nested fields, shared record identities, growth and returned values,
generic standard-library primitives and overlapping checked copies. The contract
also exercises cross-language source/binary-library consumers, exact schema
rejection before output replacement, tuple bounds failures and remaining native
aggregate-array exclusions. Native runtime tests inspect transitive collection
and reclamation through tuple arrays.
