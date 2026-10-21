# Tuples

A tuple is a fixed-size ordered group of values. Each position has its own
static type. Tuples are useful for returning several results without declaring
a named record.

```ny
fn location() -> (f64, f64)
    return (-1.6, 2.4)
end fn

fn main() -> i32
    let position = location()
    let (x, y) = position
    println(position.0)
    return 0
end fn
```

Matrix uses the same values and a prose type spelling:

```matrix
book tuple_example

location returns tuple of (floating, floating) begin paragraph
    give (negative 1.6, 2.4)
end paragraph

main returns override integer begin paragraph
    value position is location with nothing
    value (x, y) is position
    say position.0
    give 0
end paragraph
```

`(value)` groups an expression; `(value,)` constructs a one-element tuple.
Ny's corresponding type is `(T,)`; Matrix uses `tuple of (T)`. Empty tuple
values are not supported. Ny's existing `()` type spelling continues to mean
`void`; it does not construct a unit value.

Tuple types are structural: order, arity and canonical element types determine
identity. `(i64, text)` is different from `(text, i64)` and `(i32, text)`.
A fresh tuple literal can apply the usual checked scalar storage conversions
at each element; assigning an existing tuple does not change its storage
shape. Both language frontends preserve the same shape in public signatures
and NIL libraries.

Elements evaluate once, from left to right. Destructuring evaluates its tuple
initializer once and then introduces the names in the surrounding local scope.
The number of names must exactly match the tuple's arity. `_` discards a
position; its initializer still executes. Binding lists are flat; a nested
tuple can be unpacked with a second declaration. A `mut` binding in Ny or
`variable` binding in Matrix permits rebinding the extracted local values.

Positions start at zero and are written as decimal members, such as `.0` or
`.12`. Nested projection works as `result.0.1`. Invalid positions are compile
errors. Runtime indexing is unavailable: heterogeneous positions do not have
one common static result type.

Tuple slots are immutable. Assigning `pair.0 = other` or `set pair.0 to other`
is an error. Construct a replacement tuple instead. Copying a tuple preserves
any contained record or reference identities: `pair.0.field = value` can mutate
a mutable record field without replacing the tuple's slot. This follows the
existing record-handle contract, not C by-value aggregate layout.

Tuples can nest and participate in generic function signatures. Scalar, text,
and supported record/reference elements use the existing native field layouts.
Owned buffers and borrows cannot be hidden inside tuples. Record field,
pointer, and foreign-call restrictions still apply; tuples do not establish a
C-compatible ABI.

Tuple patterns support binding names, `_`, scalar literals and nested tuple
patterns in Ny `match` and Matrix `consider`. Guards execute after the pattern
has bound its names. Tuple-array loops can unpack elements in the interpreter;
arrays of tuple handles remain outside the native array contract and are
rejected by native compilation. Loop unpacking requires tuple elements with
matching arity; scalar arrays and numeric ranges cannot be unpacked. Existing
interpreter unpacking of homogeneous nested arrays remains available.
Declaration destructuring is local only.

See [NIL-0015](../design/NIL-0015-structural-tuples.md) for the representation and
[tuple contract tests](../../tests/tools/tuple_contract.py) for route coverage.
