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

**`number` is 64-bit, which is a deliberate choice against Ny.** Ny infers
`i32` for integer literals. Matrix's default is the wider type, because a
program that says it does not care about width should not overflow at 2^31.
The cost lands at the cross-language boundary: most Ny APIs take `i32`, so
calling one from Matrix may need `becomes override integer`. That is
explicit work at an explicit boundary, which is the right place for it.

**`main` is a special case.** The NIL entry contract is `main() -> i32`.
Matrix writes `main returns number` and the lowering emits `i32` for that one
function. A process exit status is eight bits in practice, so nothing is lost,
but it is a wart and it is written down rather than hidden.

## Text has no escape syntax

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
