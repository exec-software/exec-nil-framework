# Types and text

Two design changes with one motive: the surface an agent writes by default
should be small enough to hold entirely in mind, while the power a systems
language needs stays one explicit word away.

## Two tiers of type

**Tier one — what a program says unless it has a reason not to.** Five names,
and encoding, width and precision are not among the program's concerns.

| Matrix | NIL | Meaning |
| --- | --- | --- |
| `text` | `text` | Text. Encoding is the library's problem, never the program's. |
| `number` | `i64` | A whole number. |
| `floating` | `f64` | A real number. |
| `boolean` | `bool` | True or false. |
| `nothing` | `void` | No result. |

**Tier two — the machine representation, reached only through `override`.**

| Matrix | NIL | Width |
| --- | --- | --- |
| `override integer` | `i32` | signed 32-bit |
| `override long` | `i64` | signed 64-bit |
| `override byte` | `u8` | unsigned 8-bit, wrapping |
| `override double` | `f64` | IEEE double |

A machine type is **not legal in a bare type position**. `as byte` is an
error; `as override byte` is the spelling. That is the whole point of the
keyword: the default path is ergonomic, and every drop to a precise width is
marked, greppable and auditable. In a language whose artifacts are meant to be
verified against a kernel-enforced authority set, "show me every place this
program cares about exact representation" should be one search.

```text
value count  as number           is 42          !!! i64; the normal case
value pixel  as override byte    is 255         !!! u8, deliberately
value mask   as override long    is 0xFFFFFFFF  !!! exact width for bit work
value ratio  as floating         is 0.5         !!! f64
```

### Floating literals

`1e3`, `1.5E-3`, and `5e-324` are `floating` (`f64`) literals. The exponent
uses `e` or `E`, an optional sign, and one or more digits. A decimal point is
optional when an exponent is present. Write `-1e3` or `negative 1e3` for a
negative value; `minus 1e3` remains an alias. The symbolic sign must touch the
digits, while `negative` may negate any numeric expression. Subtraction still
uses `minus`; the sign inside `1e-3` belongs to the exponent.

Ny and Matrix share locale-independent conversion, including subnormal values.
Overflow and nonzero literals that round to zero are rejected in compilation
and editor analysis. Exact zero may have any exponent magnitude. `0x1E3`
remains a hexadecimal integer. See the shared
[floating literal contract](../../docs/language-reference/expressions.md#float-literals).

### Reserved and rejected

`short`, `unsigned`, `decimal`, `single` and `letter` are reserved words that
**fail with a specific diagnostic** rather than parsing as identifiers. Each is
a type Matrix would like to offer and NIL cannot carry:

- `unsigned` — NIL's only unsigned type is `u8`. `u16`, `u32` and `u64` have
  no cross-route scalar implementation and their casts fail explicitly.
- `decimal` — there is no fixed-point type in NIL at all.
- `short`, `single` — `i16` and `f32` are in the same rejected set as above.
- `letter` — there is no `char` in NIL, and `char` is listed as needing
  independent qualification. See below for where it goes instead.

Reserving them now means adding them later is additive, and that misuse today
gets a message naming the real constraint instead of "unknown identifier".

### Two consequences worth knowing

**A declared `number` is 64-bit; small integer literals still infer 32 bits.**
Ny and Matrix infer decimal literals as `i32` when representable and `i64`
otherwise. Negated boundary literals represent the signed minima directly;
values outside signed 64-bit range are rejected. Hexadecimal values through
`0xFFFFFFFF` retain 32-bit mask semantics. Matrix's intended universal
`number` default remains a separate language decision.
For example, `2147483647 plus 1` wraps before conversion even if assigned to
a `number` or passed to a `number` parameter. Bind an operand as `number`, or
cast it with `becomes override long`, before performing arithmetic that needs
64 bits. Mixed `i32`/`i64` array elements now infer `array of number` in either
order; an unannotated all-small-literal array still infers `array of override integer`.
Array types remain invariant. The remaining inference questions are tracked
in [F30](next-version-findings.md#f30--large-integer-expressions-can-narrow-before-an-i64-argument-conversion).
Ny APIs accepting `i32` may instead require `becomes override integer`.

**`main` is a special case.** The NIL entry contract is `main() -> i32`.
Matrix writes `main returns number` and the lowering emits `i32` for that one
function. A process exit status is eight bits in practice, so nothing is lost,
but it is a wart and it is written down rather than hidden.

## Ordinary text has no escape syntax

Escaping is work an agent should never do. `\"` and `\\` are a reliable source
of mangled output, and the cost is paid on every string in every file. Matrix
removes the mechanism rather than improving it.

**A quoted literal is raw and single-line.** There are no escape sequences, so
a backslash is an ordinary character:

```text
value path  as text is "C:\dir\file.txt"
value regex as text is "^[a-z]+\d*$"
```

**A literal quote, newline or tab is a named text constant.** `quote`,
`newline` and `tab` are ordinary text values, and `plus` is already text
concatenation, so no new syntax is involved:

```text
value said as text is "he said " plus quote plus "hi" plus quote
say "one" plus newline plus "two"
say "name" plus tab plus "value"
```

**Anything awkward or multi-line uses a raw block.**

```text
value banner as text is begin text
    He said "hi" and the path was C:\Users\me\file.txt
      this line keeps its extra indent
    {"json": "works too", "escaped": "nothing \n needed"}
end text
```

The block follows the language's universal `begin <kind>` / `end <kind>` shape.
Nothing inside is escaped, because the terminator is **a line-anchored keyword
rather than a character** — no content can accidentally end the literal. The
common leading indentation is removed and deeper indentation is kept; leading
and trailing blank lines are dropped. The terminator match ignores indentation
and spacing between the two words.

The one limitation: a block cannot contain a line whose only content is
`end text`. Concatenate with `plus` in that case. In exchange, every other
byte sequence in existence is writable without a single escape.

**Character literals do not exist.** There is no `letter` scalar, so `'a'` has
nothing to denote — and removing the form removes another escaping surface.
A one-letter text value is `"a"`.

### Explicit formatted text

The contextual word `formatted` opts a quoted string into interpolation:

```text
value count is 3
value offset is negative 1.6
value summary is formatted "Count: {count plus 1}; offset: {offset}"
value two-lines is formatted "First{newline}Second"
value braces is formatted "{{literal braces}} and {count}"
```

Each hole is an ordinary Matrix expression, evaluated once in written order.
Named calls and nested quoted arguments use their normal syntax, for example
`formatted "Result: {choose with first being "one", second being "two"}"`.
Scalar values and text have a shared native/interpreter conversion; other types
require an explicit formatting method or function. There is no precision or
format-specifier mini-language in the braces.
Native text keeps its existing NUL-free storage contract; interpolation does
not make embedded NUL bytes representable by the native text ABI.

Backslashes remain ordinary characters. `{{` and `}}` represent literal braces
only in this explicitly formatted form. Empty holes, extra tokens, unmatched
braces and missing quotes are errors. The string and its expressions must stay
on one physical source line; interpolate `newline` to create a multiline
result. A raw `begin text` block inside a hole therefore is not supported.
Ordinary quoted text and `begin text` keep their previous raw behavior.

`formatted` remains available as a name when it is not followed by a quote.
Expression identifiers keep their original source ranges for editor checking,
completion, signature help and supported local rename. See the shared
[formatted text contract](../../docs/language-reference/expressions.md#formatted-text).

## What the library does instead

The language keeps `text` and stops. Encoding, width conversion, parsing and
formatting are standard-library functions with a functional shape, the way
Python, Rust, C# and Java do it — not language types:

```text
link convert, to-ascii, from-ascii from ny.lang.core.convert

value bytes  is to-ascii   with source being "hello"
value back   is from-ascii with source being bytes
value parsed is convert.to-number with source being "42"
```

The receiver form is what makes this work, and it needed a codegen fix to run
at all. A method's NIL symbol is built from the impl block's target type while
the call site builds it from the receiver's *inferred* type; for a builtin
those differ, so `impl text` emitted `demo__book__text__to_ascii` while the
call emitted `String__to_ascii`. Builtins are now never module-qualified, and
`tests/language/primitive-extension.ny` keeps it fixed on four builtins.

One consequence to know, and it applies to **every** extension rather than
only to builtins: a method's symbol is keyed by the **type it extends**, never
by the extending book. So two books cannot both add `shouted` to `text`, and
two books cannot both add `summary` to one record either. The compiler now
rejects that with both book names and a remedy; it used to surface only as
NIL validation's "Duplicate NIL label".

Two same-named methods inside **one** book are still permitted, because
`ny.lang.core`'s generic `Result` impl relies on it — it declares an
associated `ok(value)` and an instance `ok(self)` together.

`letter` lives here too, as a library type over one-grapheme text, if and when
it is wanted. So do grapheme-vs-byte length, normalisation, BOM handling and
case mapping — all of which are policy decisions that do not belong in a type
name.

This is not a new direction for the project. Ny's `ny.lang.core.convert`
already hosts `Convert.try_hex_to_bytes` with checked, no-partial-output
semantics, and `ny.lang.security.encoding` already hosts Base64 and hex. The
shape exists; Matrix leans on it harder and keeps the language out of it.

## Why this serves the token-cost goal

Three effects, in rough order of size:

1. **No escaping.** Every string in every file costs fewer tokens and, more
   importantly, costs zero retry cycles. Mangled escapes are a leading cause
   of a generation that has to be regenerated.
2. **Five type names by default.** An agent that does not have to choose
   between `i8` and `i16` and `u32` does not spend tokens reasoning about it,
   and cannot spend a review cycle being wrong.
3. **`;` as an explicit statement terminator.** Several short statements share
   a line — `set a to 1; set b to 2; say a` — which is cheaper than three
   indented lines when the statements are trivial.

All three are measurable against the corpus in `samples/` and their Ny
equivalents, and measuring them is cheap. None of it is asserted here.
