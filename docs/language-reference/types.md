# Types

Ny checks source types before generating NIL. Type names recognized by the
parser are not, by themselves, a promise that every execution route implements
their storage, arithmetic, or foreign ABI. This page describes the current
scalar contract; the release profile remains a separate qualification decision.

## Scalar values

| Type | Current scalar contract |
| --- | --- |
| `i32` / `int` | Signed 32-bit integer |
| `i64` | Signed 64-bit integer |
| `u8` | Unsigned byte |
| `f64` | Double-precision floating point |
| `bool` | `true` or `false`; not an arithmetic operand |
| `text` / `String` | Text value; not an integer or pointer |
| `void` | No result from a function |

Other recognized spellings, including `i8`, `i16`, `u16`, `u32`, `u64`, and
`f32`, do **not** yet have a consistent cross-route scalar implementation.
Scalar casts to these targets fail explicitly. Do not use their acceptance in
a declaration as evidence of supported arithmetic or layout. `char`, general
references, and arbitrary foreign types also need independent qualification.

Decimal integer literals infer `i32` when representable, otherwise `i64`.
Negated literal boundaries include `-2147483648` (`i32`) and
`-9223372036854775808` (`i64`). Values outside signed 64-bit range are source
errors. Floating literals infer `f64`.

Hexadecimal values through `0xFFFFFFFF` retain `i32` mask semantics; larger
supported values infer `i64`. Positive values above `0x7FFFFFFFFFFFFFFF` are
rejected; negated `0x8000000000000000` can represent the signed minimum.
Use explicit types or casts when width matters:

```ny
let count: i64 = 10000000000
let byte: u8 = 255 as u8
let fraction: f64 = 0.5
```

An outer `i64` destination or parameter does not widen arithmetic inside its
initializer or argument. Bind or cast an operand before that arithmetic:
`(2147483647 as i64) + 1` is `2147483648`, while `2147483647 + 1` wraps at
`i32` before assignment. See [NIL-0007](../design/NIL-0007-integer-literal-widths.md)
for literal ranges, hexadecimal compatibility and the boundary between literal
inference and contextual typing.

### Arithmetic and conversions

Integer addition, subtraction, multiplication, and unary negation wrap at the
resolved width. Mixed arithmetic promotes in the order `f64`, `i64`, `i32`,
`u8`. Shifts use the left operand's width and reject negative counts or counts
greater than or equal to that width. Signed right shifts preserve the sign;
`u8` right shifts insert zeroes. Bitwise operators require integer operands.

Integer division truncates toward zero. Division and remainder reject zero
divisors and signed minimum divided by minus one. These checks prevent host
undefined behavior; they are not recoverable Ny exceptions.

`as` supports scalar targets `bool`, `i32`, `i64`, `u8`, and `f64`:

- Numeric-to-boolean conversion tests nonzero, including NaN as true.
- Integer narrowing keeps the low bits; `i32` interprets them as signed.
- Floating-to-integer conversion first requires a finite value in
  `[-2^63, 2^63)`, truncates toward zero to `i64`, then narrows if requested.
  Out-of-range values and NaN fail; they are not silently clamped.

See the executable `scalar-arithmetic-contract.ny` and
`scalar-cast-contract.ny` fixtures in `tests/language/` for boundary examples.

The interpreted routes also support explicit text conversions:

- Text to integer requires a complete signed decimal `i64`, then applies the
  same narrowing rules for `i32` or `u8`.
- Text to `f64` requires a complete finite number. Whitespace, trailing junk,
  overflow, and nonfinite values are rejected.
- Text to `bool` accepts case-insensitive `true`, `1`, `yes`, `on`, `false`,
  `0`, `no`, or `off`; other text is an error, not truthiness.
- Scalar to text retains decimal integer formatting, `0`/`1` for booleans,
  and six fractional digits for `f64`.

Native ordinary `as` does not implement text/scalar conversions and rejects
them explicitly. This is a route limitation, not an implicit alternative
conversion. Exact, statically checked identity casts preserve values such as
`ptr<i32>` or a concrete struct; they do not reinterpret storage. Changing a
pointer element type, converting integers to pointers, changing nominal types,
or erasing/recovering types through `any` is rejected.

## Arrays and pointers

The current array literal is a dynamic `array<T>`, not `array<T, N>`:

```ny
let values: array<i32> = [1, 2, 3]
println(values[0])
values[1] = 7
```

Inferred integer arrays containing both `i32` and `i64` elements use `i64`
regardless of element order. For example, `[1, 2147483648]` and
`[2147483648, 1]` both infer `array<i64>`. This also applies to typed variable
elements. Array types remain invariant: an `array<i64>` cannot be passed as
`array<i32>` through an implicit element conversion.

Typed pointers use `ptr<T>`. Allocation and deallocation are explicit:

```ny
let values = allocate<i32>(2)
values[0] = 40
values[1] = 2
println(values[0] + values[1])
deallocate(values)
```

Do not infer an ownership or borrowing guarantee from this syntax. Pointer,
array, tuple, and native-call layouts have narrower contracts than a general
C-compatible ABI. Standard-library collections are separate APIs, not an
implied built-in map literal or interchangeable `vec<T>` syntax.

Native array elements include `i32`, `i64`, `u8`, `bool`, `f64`, NUL-free text,
and supported structural tuples. Scalar out-of-range reads return the element's
zero value; tuple-array reads fail because a tuple has no default zero value.
Out-of-range writes do nothing. `push` requires a compatible element and
preserves aliases when the descriptor grows. Use an explicit cast before
pushing an integer into `array<f64>`; storage does not reinterpret its bits.
Tuple arrays retain one exact ordered schema and permit whole-element
replacement while tuple slots stay immutable. See the [tuple reference](tuples.md).
Native arrays of records, enums and nested arrays, and raw
`ptr<f64>`/`ptr<text>`, remain outside this subset.

## Structs and generic enums

Struct literals use braces and named fields:

```ny
struct Point
    x: i64
    y: i64
end struct

fn main() -> i32
    let point = Point { x: 1, y: 2 }
    println(point.x)
    return 0
end fn
```

Field default initializers in struct or class declarations are not implemented
and are rejected. Initialize fields explicitly in construction expressions;
a declaration such as `value: i32 = 37` must not be mistaken for a working
default.

Native nested structs and `ref<Struct>` use the same shared object-handle
behavior as the interpreter. Updating an object through a field or reference
is visible through its other aliases. This is not by-value C struct layout,
automatic ownership, or Rust-style borrowing. Scalar references and recursive
layouts are not implemented. Source `ref value` accepts only a local or
parameter with a nominal struct/class type (including a concrete generic
struct). It creates an alias to that object, not to the variable's storage:
rebinding the original local does not redirect an existing reference.

Use `alias.field` and `alias.method()` directly. Explicit `*alias` reads/writes
are rejected for `ref<T>`; dereference syntax belongs to raw `ptr<T>` operations.
Scalar, array, enum, function, raw-pointer, erased `any`, nested-reference,
global and temporary/field-expression reference creation fail semantic
checking. Emitted reference operations still require proof with `--no-typecheck`;
diagnostic continuation cannot generate the unsupported operation. Uninstantiated
generic bodies may not reach generation. Declaration-only `ref<T>` types and
generic constraints do not imply an executable borrow/lifetime contract.

### Classes

Non-generic concrete classes support instance methods, inherited fields and
methods, and checked overrides on the interpreted and native routes:

```ny
class Counter
    value: i32
    pub virtual fn read(self) -> i32
        return self.value
    end fn
end class

class OffsetCounter inherits Counter
    offset: i32
    pub override fn read(self) -> i32
        return self.value + self.offset
    end fn
end class

fn main() -> i32
    let counter = OffsetCounter { value: 40, offset: 2 }
    println(counter.read())
    return 0
end fn
```

Every construction must initialize every inherited and local field exactly
once with a compatible type. Fields currently have public access; method
visibility is separate: `pub` methods may be called by consumers, while
unmarked methods may only be called from the declaring class. Calls require
an explicit first `self` parameter and validate argument count and types.
An explicit `self: ref<Self>` requires a matching reference receiver.
Scalar method arguments retain the declared parameter representation, including
signed 32-bit narrowing for `i32`. Use explicit casts for integer arguments to
`f64` or `u8`; class calls do not add implicit conversions to the source type
compatibility rules. Handles and text are not implicitly converted to scalar
arguments. Variadic class method execution is rejected.

Plain class values support checked ancestor views at explicit typed boundaries.
An ancestor view preserves dynamic identity and shared field storage. Virtual
calls, including calls on `self` inside an inherited helper, select the dynamic
override. Nonvirtual methods retain their declaring-owner body and scope.
Methods can alias, pass or return `self` according to its owner type. See
[classes and dispatch](classes.md) for the exact conversion and receiver rules.

Module-qualified class names remain distinct in textual and binary NIL.
Inheritance cycles, duplicate or ambiguous fields, ambiguous inherited
methods, incompatible overrides, reduced public visibility, and hiding a
non-virtual inherited method are rejected. These checks do not establish a
foreign virtual-table ABI, ownership/borrowing guarantees, or automatic destructors.

Abstract obligations use the nearest declaration: an abstract override removes
an older concrete implementation, and a concrete descendant must supply a new
matching implementation. A class with its own abstract method must also be
declared `abstract`. An abstract class may define a concrete helper that calls
an abstract method on `self`. Concrete helpers are emitted once for their
declaring owner; abstract method declarations have no executable body.
Abstract-typed values may be passed and stored, and their virtual calls require
an actual concrete implementation. Abstract construction remains rejected.

Enums can carry a typed payload. Construction uses a dot; match arms use `->`:

```ny
enum Option<T>
    Some(T)
    None
end enum

fn main() -> i32
    let value: Option<i32> = Option.Some(42)
    match value
        Option.Some(payload) -> println(payload)
        Option.None -> println("no value")
    end match
    return 0
end fn
```

The standard library provides `Option<T>` and `Result<T, E>` through its core
modules. `Result` uses `Ok(value)` and `Error(error)` variants. Import those
declarations instead of redeclaring them in an application. Concrete generic
payload and field types are preserved in current textual and binary NIL; see
[the NIL format contract](../advanced/nil-format.md).
