# Statements

Statements are instructions that perform actions but don't produce values.

## Variable Declarations

### Immutable Binding
```ny
let x = 42
let name = "Ny"
let items = [1, 2, 3]
```

### Mutable Binding
```ny
let mut counter = 0
counter = counter + 1
```

### Constant
```ny
const MAX_SIZE = 100
```

## Assignment

```ny
x = 10
object.field = new_value
array[0] = value
```

## Control Flow Statements

### If Statement
```ny
if x > 0 then
    println("positive")
elseif x < 0 then
    println("negative")
else
    println("zero")
end if
```

### Match Statement
```ny
match value
    1 -> println("one")
    2 -> println("two")
    _ -> println("other")
end match
```

### While Loop
```ny
let mut i = 0
while i < 10
    println(i)
    i = i + 1
end while
```

### For Loop
```ny
for item in items
    println(item)
end for

for i in range(0, 10)
    println(i)
end for
```

### Infinite Loop
```ny
loop
    if done then
        break
    end if
    # do work
end loop
```

### Break and Continue
```ny
for item in items
    if skip then
        continue
    end if
    if found then
        break
    end if
end for
```

## Return Statement

```ny
fn add(a: i32, b: i32) -> i32
    return a + b
end fn
```

## Faults and Cleanup

Synchronous fault handling uses `attempt`, typed `fault` handlers, `recover`
cleanup and `raise`. See [faults and cleanup](faults.md) for exact matching,
opaque catch-all tokens, return/loop cleanup and fatal-error boundaries.

## Expression as Statement

Any expression can be used as a statement (value is discarded):
```ny
println("hello")  # function call
x + y            # arithmetic
foo()             # method call
```

Bindings inside a branch or loop have lexical storage separate from the
enclosing scope. A standalone `do`-block syntax is not part of the executable
contract described here.

## Import Statement

```ny
import ny.lang.core
import my.module
import some.package as sp
```

## Module Declaration

```ny
module my.module

# module contents

end module
```

## Package Declaration

```ny
package my.package

pub use module_name
```
