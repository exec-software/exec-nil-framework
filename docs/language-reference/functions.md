# Functions

This page describes the function surface that is executable today. Proposed
features are marked explicitly; syntax acceptance alone is not treated as a
runtime guarantee.

## Declarations

`fn` and `function` are accepted declaration keywords. `fun` is retained as a
legacy spelling.

```ny
fn greet(name: text) -> text
    return "Hello, " + name
end fn

fn announce(message: text)
    println(message)
end fn
```

Use explicit `return` for a non-void function. Expression statements discard
their values, including the final expression in a function body. Reaching the
end of a non-void function without returning fails; it does not supply an
implicit result. Void calls and assignments produce no placeholder value.
The arrow-body shorthand `fn square(x) -> i32 => x * x` is not implemented.

## Parameters

Required positional parameters and ordinary recursion work across textual NIL,
binary NIL, both bundle formats, LLVM JIT, and LLVM AOT for the verified type
subset.

```ny
fn factorial(value: i32) -> i32
    if value <= 1 then
        return 1
    end if
    return value * factorial(value - 1)
end fn
```

Supported native function boundaries currently include integer and boolean
words, `f64`, named/concrete generic enums, text, `array<i32/i64/u8/bool/f64/text>`,
arrays of supported structural tuples, `ptr<i32/i64/u8/bool>`, and validated
concrete struct layouts. Generic struct
schemas are materialized for supported field layouts, including `f64`/`u8`
fields, nested struct handles and `ref<Struct>`. These references preserve shared object identity;
they are not scalar stack references or a borrow checker. Scalar references,
slices, unsupported scalar widths, and recursive layouts remain fail-closed in
the native backend.

### Mutable parameters

`mut` makes the callee's local parameter binding mutable. It does not mutate an
ordinary scalar variable in the caller.

```ny
fn increment_local(mut value: i32) -> i32
    value = value + 1
    return value
end fn

let original = 5
let incremented = increment_local(original)
# original is 5; incremented is 6
```

An aggregate parameter or `ref<Struct>` shares object identity with its caller;
field mutations are visible through aliases, while rebinding the parameter
does not redirect the caller's binding. Scalar `ref` creation is rejected.
These shared handles do not carry borrowing or lifetime proofs. Explicit
[owned buffers](../advanced/owned-buffers.md) have a separate checked local
ownership/borrowing contract; owners and loans cannot be function parameters.

### Default parameters

Default expressions are retained in the syntax tree, but omitted arguments are
currently rejected. Callers must pass every declared parameter. Executable
default insertion is a design target.

### Variadic parameters

A final `..name: type` parameter collects trailing arguments into
`array<type>`. The registered conformance fixture verifies zero, one, and
multiple `i32` arguments on all six execution routes.

```ny
fn sum_all(..values: i32) -> i32
    let mut total: i32 = 0
    let mut index: i32 = 0
    while index < values.len()
        total = total + values[index]
        index = index + 1
    end while
    return total
end fn

sum_all()
sum_all(1, 2, 3, 4)
```

Other variadic element layouts do not yet have the same route evidence.

## Methods and traits

Concrete methods in `impl` blocks have interpreted and focused JIT/AOT
coverage, including multiple trait implementations on concrete generic
receivers. `impl Trait for Type` must name a declared trait and supply its
required signatures, including concrete type arguments, `Self`, mutability,
variadic shape, and method generic bounds. This is static conformance, not
dynamic trait-object dispatch; see the [trait contract](traits.md).

```ny
struct Counter
    value: i32
end struct

impl Counter
    fn increment(self) -> i32
        return self.value + 1
    end fn
end impl
```

## Partial or unimplemented features

### Overloading

Signature-based overload resolution is a design target. Duplicate function
names currently produce duplicate NIL labels or name-only native resolution;
do not declare overload sets yet.

### Lambdas, closures, and higher-order functions

Named function values and noncapturing lambda calls have interpreted
call-frame coverage. Captures are rejected explicitly. Native indirect calls,
closures, and the general higher-order function ABI remain incomplete.

## Built-ins and intrinsic wrappers

The `println(value)` statement takes exactly one argument and appends a newline.
Zero or multiple arguments are rejected before emitting NIL or native output,
including with `--no-typecheck`. Use `println("")` for a blank line and separate
statements to print several values. Text and supported scalar values have
cross-route output coverage; printing arbitrary handles is not a portable
serialization contract. Standard-library console functions use their declared
signatures and are separate from this statement.

Runtime-backed declarations use an annotation such as:

```ny
@intrinsic("console.writeln")
fn write_line(value: text)
end fn
```

An annotation does not by itself provide an implementation. The named runtime
provider must exist on every claimed route. The interpreter implements a broad
but explicitly audited intrinsic subset; native support is provider-specific.

## Annotations and foreign interfaces

Structured declaration annotations are retained in the frontend, and selected
annotations such as `@intrinsic`, `@interop`, `@inline`, and `@ai_context` have
special handling. Current binary executable NIL v3 preserves function
annotations, flags, and declaration lines alongside type schemas; legacy v1
does not. Class-bearing artifacts use v4, and `--debug-info` uses v5 for source
paths and lexical bindings. Native Linux x86_64 output can separately opt into
[executable context](../advanced/executable-context.md) with `--embed-context`;
`--inspect-context` reads retained metadata without executing the artifact.
Foreign-interface declarations remain experimental and require a built,
linked provider; their presence in source is not evidence that the function
can execute.

For current route evidence and its limits, see the
[SDK capability catalog](../../scripts/sdk_capabilities.json) and the
[engineering notebook](../../notebook/README.md).
