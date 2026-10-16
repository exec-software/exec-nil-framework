# Modules

Modules organize code into reusable units with their own namespace.

## Module Declaration

### Basic Module
```ny
module math.utils

# Functions in this module
pub fn add(a: i32, b: i32) -> i32
    return a + b
end fn

pub fn subtract(a: i32, b: i32) -> i32
    return a - b
end fn
```

### Private Functions
```ny
module secrets

fn internal_helper()
    # Only accessible within this module
    return
end fn

pub fn public_function()
    internal_helper()
    return
end fn
```

## Importing Modules

### Full Import
```ny
import math.utils

let result = math.utils.add(1, 2)
```

### Selective Import
```ny
import math.utils.{add, subtract}

let result = add(1, 2)
```

### Alias Import
```ny
import math.utils
namespace mu = math.utils

let result = mu.add(1, 2)
```

### Using Module Contents
```ny
use math.utils

# Can now use add directly
let result = add(1, 2)
```

## Qualified module names

```ny
module outer.inner

pub fn nested_function() -> i32
    return 42
end fn
```

## Module File Organization

Use an explicit module declaration for stable namespace identity. Source paths
help import discovery, but must not be confused with a variable's storage name.
A conventional layout is:

```
src/
├── main.ny           # module main
├── math.ny           # module math
└── math/
    ├── ops.ny        # module math.ops
    └── helpers.ny    # module math.helpers
```

## Package Declaration

A package groups related modules:

```ny
package my.application

pub use math.utils
pub use math.ops
```

## Re-exports

Modules can re-export symbols from other modules:

```ny
module my_lib

pub use other_module.{FuncA, FuncB, StructC}
```

## Visibility Modifiers

| Modifier | Description |
|----------|-------------|
| `pub` | Public - accessible from anywhere |
| `pub(crate)` | Parsed visibility form; crate-wide release guarantees are not established |
| (none) | Private - only within module |

## Module Initialization

Module-level variable initializers run in declaration order in the assembled
program. Arbitrary top-level statements are not a supported initialization
mechanism. An explicitly typed forward global can type-check, but reading it
before its initializer runs fails at runtime rather than returning an invented
zero value.

```ny
module initializer

let seed: i32 = 40
let answer: i32 = seed + 2

pub fn get_value() -> i32
    return answer
end fn
```

## Module globals

Globals belong to their declared module. Same-spelled globals in different
modules have distinct `module::name` storage identities through textual and
binary NIL, interpreter/bundles, and native compilation. Use `pub` to expose
them and `::` for an explicit qualified reference:

```ny
module settings
pub let mut level: i32 = 1

module client
let level: i32 = 99

fn main() -> i32
    settings::level = 3
    println(settings::level)
    println(level)
    return 0
end fn
```

Named/glob imports and namespace aliases resolve to the same declared storage.
Lexical locals shadow those names. Two imports exposing different globals with
the same name are ambiguous unless qualified; private globals and immutable
assignments reject. A duplicate declaration in the same module also rejects.
Generic functions and methods retain their declaring module when specialized.

Aggregate globals still use the interpreted representation only. These namespace
rules do not establish native separate compilation or a stable foreign ABI.
