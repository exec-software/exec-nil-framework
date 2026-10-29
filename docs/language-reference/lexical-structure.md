# Lexical Structure

This document describes the lexical structure of the Ny programming language.

## Source Files

Ny source files use the `.ny` extension. The encoding is UTF-8.

## Tokens

Ny consists of the following token types:

### Identifiers

```
identifier = letter { letter | digit | '_' }
```

Examples: `x`, `myFunction`, `counter_1`, `_private`

### Keywords

| Keyword | Description |
|---------|-------------|
| `fn` | Function declaration |
| `let` | Immutable variable |
| `mut`, `mutable` | Mutable binding modifier |
| `ref`, `reference` | Reference type, expression, or parameter modifier |
| `const` | Constant |
| `if` | Conditional |
| `else` | Alternative branch |
| `elseif` | Else-if branch |
| `then` | Then branch (optional) |
| `end` | End of block |
| `return` | Return value |
| `println` | Print line |
| `while` | While loop |
| `for` | For loop |
| `in` | Iterator binding |
| `loop` | Infinite loop |
| `break` | Break loop |
| `continue` | Continue loop |
| `import` | Import module |
| `package` | Package declaration |
| `module` | Module declaration |
| `pub` | Public visibility |
| `struct` | Struct declaration |
| `enum` | Enum declaration |
| `type` | Type alias |
| `as` | Type cast |
| `match` | Pattern matching |
| `when` | Match arm |
| `true` | Boolean true |
| `false` | Boolean false |

### Operators

| Operator | Description |
|----------|-------------|
| `+` | Addition |
| `-` | Subtraction |
| `*` | Multiplication |
| `/` | Division |
| `=` | Assignment |
| `==` | Equality |
| `!=` | Inequality |
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less or equal |
| `>=` | Greater or equal |
| `::` | Double colon (path qualifier) |
| `.` | Member access |
| `..` | Range operator |
| `->` | Arrow (type annotation, match arm) |
| `@` | Annotation prefix |

### Literals

#### Numbers

```
number_literal = digit { digit } [ '.' digit { digit } ]
                 [ ('e' | 'E') [ '+' | '-' ] digit { digit } ]
               | ('0x' | '0X') hex_digit { hex_digit }
```

Examples: `42`, `3.14`, `1e3`, `1.5E-3`, `0xFF`. A decimal point or exponent
makes the literal `f64`; see the [floating literal contract](expressions.md#float-literals)
for range diagnostics. A fractional part requires digits on both sides of the
point, leaving `..` available for ranges.

#### Strings

```
string_literal = '"' { character } '"'
```

Examples: `"Hello, World!"`, `""`

Explicit formatted text starts with `$"` and ends with a matching quote on the
same source line. Braces contain Ny expressions; doubled braces are literal
text. See [formatted text](expressions.md#formatted-text) for evaluation,
conversion and escaping rules.

#### Characters

```
char_literal = '\'' character '\''
```

Examples: `'a'`, `'A'`, `'\n'`

#### Annotations

```
annotation = '@' qualified_name [ '(' annotation_arguments ')' ]
```

Examples: `@inline`, `@doc(summary="Advance the counter")`,
`@Company.review(approved=true)`. See [annotations](annotations.md) for the
scalar metadata profile and Matrix's equivalent prose syntax.

## Whitespace

Ny ignores the following whitespace characters:
- Space (`' '`)
- Tab (`'\t'`)
- Newline (`'\n'`)
- Carriage return (`'\r'`)

## Comments

```
comment = '//' { character } newline
```

Everything after `//` on a line is a comment.

Example:
```ny
// This is a comment
let x = 42  // inline comment
```

## Block Comments

Ny does not support multi-line comments. Each line must use `//` for comments.
