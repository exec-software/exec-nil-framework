# NIL-0019: Explicit binding-reference parameters

- **Status:** Accepted; initial scalar/text implementation
- **Scope:** Ny and Matrix parameter syntax, shared checking and NIL lowering,
  VM/native execution, source tooling

## Decision

Parameter declarations distinguish an immutable local binding, a mutable local
binding, and a reference to the caller's binding. Both languages accept `mut`
and `mutable` for callee-local assignment, and `ref` and `reference` for
caller-binding parameters. Matrix also retains `variable` as a compatibility
alias for local mutability. The caller passes an ordinary value to a locally
mutable parameter.

Caller-binding mutation requires a separate modifier at both ends of the call:

| Operation | Ny | Matrix |
| --- | --- | --- |
| Mutable local parameter | `mut amount: i64` | `mutable amount as number` |
| Caller-binding parameter | `ref amount: i64` | `reference amount as number` |
| Explicit binding argument | `update(ref total)` | `update with amount being reference to total` |

These examples use the canonical display spellings: short forms in Ny and long
forms in Matrix. Either spelling works in either language without changing its
grammar. Ny accepts `update(reference total)`; Matrix accepts
`update with amount being ref to total`, retaining its `to` keyword. The aliases
also apply to existing uses such as Ny `let mutable`, Matrix `on mut receiver`,
Ny `reference<Record>`, and Matrix `ref to Record`.

All four spellings are reserved keywords in both languages. Existing bindings
named `mutable` or `reference` in Ny, or `mut` or `ref` in Matrix, must be renamed
or, in Matrix, written as escaped identifiers.

The parameter's source type remains its scalar/text value type. Assigning the
parameter updates the referenced binding; reading or returning the parameter
produces its value. A reference parameter can forward the same binding to
another reference parameter with the explicit argument syntax.

Writes are immediate. Passing one binding to multiple parameters creates
aliases to the same storage, and a later read observes earlier writes. A fault
does not undo completed writes. Copy-back on normal return would obscure both
aliasing and fault behavior, so it is not the contract.

## Initial checked profile

The supported value types are exactly `i32`, `i64`, `u8`, `bool`, `f64`, and
`text`, including their ordinary frontend aliases. Parameter and target types
must match exactly: reference passing does not apply numeric promotion or
create a converted temporary.

Targets are mutable local bindings, mutable by-value parameters, or existing
reference parameters. Immutable bindings, globals, fields, indexed elements,
temporary expressions, and unsupported value types are rejected. References
cannot be stored as values, returned, captured, or inserted into aggregates.
Returning the referenced scalar/text value is allowed.

Reference parameters are restricted to ordinary non-generic named functions.
Methods, anonymous functions, function values, foreign/intrinsic boundaries,
defaulted reference parameters, and variadic reference parameters are outside
the initial profile. These exclusions are checked, including before emission
when ordinary type checking is disabled.

Matrix's `and` parameter separator recognizes optional `more` and parameter-mode
modifiers before the next `name as type` sequence. `more mutable` remains a
callee-local variadic binding; `more reference` is rejected. A receiver's
existing `on mutable receiver` clause is unchanged.

## Relationship to object references

`ref<Struct>` in Ny and `reference to Record` in Matrix preserve shared object
identity. Rebinding a local holding an object handle does not redirect its
existing aliases. That behavior is unchanged.

The new mode is expressed before a parameter's name, not in its value type:
`reference amount as number` addresses a caller binding, whereas
`amount as reference to Record` accepts an object handle. It does not create
first-class scalar reference types or provide a general borrow checker.

## Lowering and lifetime

The compiler represents each addressed lexical binding with a private typed
cell whose sole field is `value`. The reserved schema name is
`$nil.binding.cell.T`, marked `@nil_binding_cell`; a lowered reference parameter
has the NIL type `ref<$nil.binding.cell.T>`. Existing typed record creation,
field access, and direct-call instructions carry the implementation.

Cell identity follows the compiler's lexical storage slot. Shadowed source
names remain separate bindings. Addressing the same binding twice uses the
same cell, and forwarding an existing reference passes that cell without
copying or scheduling a later write-back. Cell initialization occurs before
user operations that access the addressed binding.

The VM uses its shared record storage. Native cells live within the creating
function's stack frame. A shared NIL validation pass constrains their schemas,
uses, direct calls, and escape paths before execution or native lowering.
The synchronous, nonescaping calling contract keeps the owning frame alive
until all callees using its cells return. The internal representation is not
a public FFI layout or user-constructible reference type.

## Tooling

Source signatures display `mutable` or `reference` in Matrix and `mut` or
`ref` in Ny, regardless of the spelling used in source. The `nil.signature.v1`
parameter records add boolean `mutable` and `reference` fields; clients that
read older compiler output may treat
those fields as absent. The value type is still the source scalar/text type.

Source debug metadata maps an addressed name to its cell slot using the
internal `binding_ref<T>` marker. Source inspection unwraps the cell's `value`
and exposes type `T`, so locals, watches, evaluation, and DAP present the
program's binding. Instruction-only inspection retains its existing storage
view. The lightweight language server and TextMate grammar recognize the
parameter modifiers independently of compiler analysis.

## Validation scope

Qualification covers both frontends and the shared VM/native execution paths:
local mutability, all supported reference value types, exact-type rejection,
forwarding, repeated aliases, recursion, lexical shadowing, and writes observed
after faults. Invalid escape and declaration forms must fail before replacing
an output artifact, including with `--no-typecheck`.

Text operations and array reads assigned through reference parameters are
covered on all execution routes, including unmanaged native execution.
Ordinary callbacks can coexist with reference parameters on the existing
VM and bundle routes; this feature does not add native indirect-call support.
Reference arguments still require direct calls, including in serialized NIL.

Source-import consumers must retain parameter modes across the Ny/Matrix
boundary. Serialized NIL validation must reject forged cell schemas and
escaping uses. Editor checks cover modifier completion, parameter locations,
highlighting, signature labels, and source debugger values. These are the
acceptance requirements for the profile; the existence of this record alone
does not establish a passing test run or broader reference support.
