# Expressions

Expressions produce values. Statements such as assignments and void calls do
not supply placeholder values.

## Literals

### Integer Literals
```ny
42          # i32
42 as i64   # explicit width
0xFF        # hex
```

### Float Literals
```ny
3.14        # f64
3.14 as f64
```

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
Matrix text uses [raw literals and named constants](../../matrix/docs/types-and-text.md#text-has-no-escape-syntax)
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
