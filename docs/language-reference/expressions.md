# Expressions

Expressions produce values. Statements such as assignments and void calls do
not supply placeholder values.

## Collected queries and text patterns

`collect query ... end query` produces a typed array from a source array,
with optional filtering, stable ordering and pagination before projection.
See [collected queries](../standard-library/queries.md) for both Ny and Matrix
syntax and evaluation rules. Named [text patterns](../standard-library/text-patterns.md)
are immutable text-source declarations consumed by the shared pattern library.

## Literals

### Integer Literals
```ny
42          # i32
42 as i64   # explicit width
0xFF        # hex
2147483648  # inferred i64
```

Decimal literals infer `i32` or `i64` by representable range. Hexadecimal
literals through `0xFFFFFFFF` retain 32-bit mask semantics. Out-of-range
integers are rejected before code generation; see the
[integer width contract](types.md#scalar-values).

### Float Literals
```ny
3.14        # f64
3.14 as f64
1e3         # f64, even without a decimal point
1.5E-3      # 0.0015
5e-324      # smallest positive f64, rounded from decimal
```

Decimal literals containing a fractional part or an `e`/`E` exponent infer
`f64`. An exponent may have a `+` or `-` sign and must contain at least one
digit. Leading signs are unary operators. Hexadecimal integers such as
`0xDEAD` retain their integer meaning.

Conversion is independent of the host locale and accepts finite normal and
subnormal results. Overflow and nonzero literals that round to zero are compile
errors, including during editor analysis. Exact zero remains valid with any
exponent magnitude; unary minus preserves its negative sign. These rules apply
to Ny and Matrix and to textual/binary NIL loading.

### String Literals
```ny
"Hello, World!"
"Multi-line
string"
```

Ny text literals decode `\"`, `\\`, `\n`, `\r`, `\t`, `\v` (vertical tab),
`\f` (form feed), and `\0`. Native text does not support embedded NUL.
Other escape spellings are unsupported; current diagnostic limitations are
tracked in [F20](../../matrix/docs/next-version-findings.md#f20--ny-silently-consumes-unsupported-escapes-corrupting-whitespace-helpers).
Matrix text uses [raw literals and named constants](../../matrix/docs/types-and-text.md#ordinary-text-has-no-escape-syntax)
instead of Ny escape syntax.

### Character Literals
```ny
'a'
'\n'        # newline
```

Integer/float suffixes such as `42i64` and `3.14f32`, and hexadecimal character
escapes such as `'\xFF'`, are not implemented syntax.

### Boolean Literals
```ny
true
false
```

## Arithmetic Operators

```ny
let sum = a + b
let diff = a - b
let prod = a * b
let quot = a / b
let mod = a % b
let neg = -x
```

## Comparison Operators

```ny
a == b      # equal
a != b      # not equal
a < b       # less than
a > b       # greater than
a <= b      # less or equal
a >= b      # greater or equal
```

## Logical Operators

```ny
a and b     # logical and
a or b      # logical or
not a       # logical not
```

## Bitwise Operators

```ny
a & b       # and
a | b       # or
a ^ b       # xor
~a          # not
a << b      # left shift
a >> b      # right shift
```

## Range Expressions

```ny
for index in range(1, 5)
    println(index)
end for
```

The loop above visits 1 through 4. `range(end)` starts at zero. Standalone
range values and inclusive `...` syntax are not implemented.

## Member Access

```ny
object.field           # field access
object.method()        # method call
array[0]               # index
```

## Function Calls

```ny
let result = foo(arg1, arg2)
let result = obj.method()
```

## Control Flow Expressions

### If Expression
```ny
let x = if condition then value1 else value2 end if

# Or as statement
if x > 0 then
    println("positive")
else
    println("non-positive")
end if
```

### Match Expression
```ny
let message = match value {
    1 -> "one";
    2 -> "two";
    _ -> "other";
}
```

Expression matches require a final unguarded `_` arm and one value-producing
expression per arm. Void calls, assignments, and statement blocks cannot supply
that value. Guards use `pattern when condition -> value`. A parenthesized match
used as an expression statement discards exactly its selected result.

Native control-flow joins preserve pending expression operands and selected
values, including nested `if`/`match` expressions and later call arguments.
Incoming branches must agree on stack height and exact ABI type; mismatched
raw NIL fails explicitly. Statement-form matches have a separate contract.

`do` blocks do not provide expression values. Use an explicitly returning
function when a computation needs several statements.

### Switch Expression

`switch` can run statements and fall through cases before producing a value.
Ny uses `break value`; Matrix uses `stop switching with value`. Every normal
exit must provide a compatible result, and a default is required. See the
[switch reference](switch.md) for both surfaces and their exit rules.

## Formatted text

An explicit formatted string evaluates expressions inside braces:

```ny
let count = 3
let offset = -1.6
let message = $"Count: {count + 1}; offset: {offset}"
let braces = $"{{literal braces}} and {count}"
```

Each expression uses ordinary Ny syntax, including calls, member access,
conditionals and nested quoted strings. Expressions are evaluated exactly once,
from left to right. The result is `text`. A hole must produce `text`, `bool`,
`i32`, `i64`, `u8` or `f64`; convert other values explicitly with an appropriate
library function or method. Numeric text is independent of the current locale,
and the same conversion is used by the interpreter and native compiler.
Native text retains its existing NUL-free restriction; formatted text does not
change the text storage ABI or permit embedded NUL constants natively.

`{{` and `}}` insert literal braces. Ny's normal backslash escapes still apply
to the literal portions, so `\n` inserts a newline in the result. Embedded
expressions keep their own string delimiters; no extra escaping is needed for
`$"Result: {choose("first", "second")}"`.

The entire explicit string, including its expressions, must fit on one source
line. Empty holes, trailing tokens, unmatched braces and missing quotes are
errors. Formatting flags and precision specifiers are not part of this syntax;
use an explicit formatting function inside a hole when needed.

Legacy ordinary strings such as `"Hello {name}"` retain identifier-only
interpolation for compatibility. Use the explicit form for expression
interpolation and editable source locations inside holes. Existing literal
`"{count + 1}"` text does not acquire new expression semantics.

Matrix uses the same semantics with a contextual word:

```text
value count is 3
value offset is negative 1.6
value message is formatted "Count: {count plus 1}; offset: {offset}"
```

Ordinary Matrix quoted strings and `begin text` blocks remain raw. Within
formatted Matrix text, backslashes remain ordinary characters; use the named
`newline`, `tab` and `quote` values where needed, such as
`formatted "First{newline}Second"`. Braces delimit ordinary Matrix expressions,
including named calls such as `{choose with first being "one"}`.

## Lambda / Anonymous Functions

```ny
let add = fn(a: i32, b: i32) -> i32
    return a + b
end fn

```

Noncapturing lambda invocation is covered on interpreted routes. Captures,
shorthand lambdas, and native indirect calls are not supported by this example.

## Type Cast

```ny
let n: f64 = 42 as f64
```

See [types](types.md) for the scalar conversion contract. Legacy interpreted
text casts are not evidence of equivalent native conversion behavior.
