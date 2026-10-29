# Ny surface inventory and Matrix mapping

This is the complete inventory of what the Ny front end accepts and what the
NIL routes actually execute, with the Matrix spelling for each item. It is the
specification source for the Matrix front end: a construct with no row here has
no Matrix syntax.

Three columns matter. **Ny** is the existing surface. **Matrix** is the
proposed surface. **Route state** repeats Ny's own evidence boundary, because
Matrix must not out-promise it. `executed` means route fixtures exist;
`parsed` means the syntax is accepted but no route executes it; `rejected`
means the compiler fails closed.

---

## 1. Lexical structure

| Ny | Matrix | Route state |
| --- | --- | --- |
| `.ny`, UTF-8 | `.matrix`, UTF-8 | — |
| `// line comment` | `!!! line comment` | — |
| no block comments | no block comments; a bare `!!!` is an empty comment line | — |
| `identifier = letter {letter\|digit\|'_'}` | adds `-` as a word joiner: `frame-kind`; `` `quoted` `` escapes a keyword collision | — |
| case-sensitive | **case-insensitive**, ASCII folding only | — |
| `MyType` vs `mytype` are distinct | one name; the declaration site fixes the emitted spelling | — |
| `0xFF`, `42`, `3.14` | same | executed |
| `"text"`, `'c'` | same | executed |
| suffixes `42i64`, `3.14f32` | no spelling | rejected in Ny |
| `'\xFF'` hex char escape | no spelling | rejected in Ny |
| `@annotation` | `@annotation` / `note <name>` / `context "..."` | see §12 |

The draft offered two comment forms: `!!!` plus text, and a block opened and
closed by a line that is exactly `!!!`. They cannot coexist. A bare `!!!`
written as a blank line between paragraphs of a line-comment run is
indistinguishable from a block delimiter, and long file headers are full of
such lines — the showcase sample has 25 of them.

Matrix therefore keeps the line form only, which is also what Ny does. A bare
`!!!` is an empty comment line. If a block form is wanted later it needs its
own delimiter, not a reuse of this one.

### Names fold; the declaration site fixes the symbol

An identifier's **lookup key** is its ASCII-lowercased spelling with every `-`
and `_` removed, so `frame-kind`, `FRAME-KIND`, `frame_kind` and `framekind`
are one name. Underscores fold with hyphens, which is what lets Matrix spell
Ny's snake_case library names in kebab-case: `read-regular-bytes` resolves to
`ny.lang.io`'s `read_regular_bytes`.

A declaration's **emitted NIL symbol** is its exact spelling at the
declaration site, with `-` rewritten to `_`. Two constraints force this, and
neither is negotiable:

- Ny identifiers are `letter { letter | digit | '_' }` — **no hyphen**
  (`docs/language-reference/lexical-structure.md`). A Matrix type declared
  `frame-kind` must therefore reach NIL as `frame_kind`, or no Ny program
  could ever name it.
- NIL identity is **case-significant**. `faults.md` requires exact nominal
  record identity where "case and module punctuation are significant", and
  `classes.md` requires class names to "preserve exact module and case
  identity". Matrix cannot fold case on the wire without breaking fault
  matching and class ancestry across the language boundary.

The consequence to accept deliberately: Matrix-internal references fold,
cross-language references from Ny do not. An Ny program names a Matrix
declaration by the declaration's exact spelling. The asymmetry is unavoidable
— Ny is case-sensitive and has a published release candidate — and putting the
canonical spelling at the declaration rather than the reference is what keeps
it from becoming ambiguity.

Folding is **ASCII-only**; a non-ASCII letter compares byte-exact. Full
Unicode folding is locale-dependent (Turkish dotless i, German sharp s), and
names that fold together while looking different are a confusable-identifier
hazard rather than a convenience.

Capitals carry no meaning once folding applies, so `SCREAMING_CASE` cannot
mark a constant — `MAX-PAYLOAD` and `max-payload` are the same name. The
`constant` keyword marks constants; capitals are a human signal with no
semantic weight.

---

## 2. Scalar types

| Ny | Matrix | Route state |
| --- | --- | --- |
| `@name(value, key=value)` | same `@` spelling, or `note name with value, key being value` | scalar metadata; parenthesized forms may span lines |
| `@doc(summary="...")` | same `@` spelling, or `note doc with summary being "..."` | retained metadata; no automatic decorator execution |
| `@include`, macros, conditional directives | same directive spellings | shared preprocessor; Matrix literals/annotation regions protected; source-edit locations conservative after transformation |
| `i32` / `int` | `integer` | executed |
| `i64` | `long` | executed |
| `u8` | `byte` | executed, wrapping (modulo 256) |
| `f64` | `real` | executed |
| `bool` | `boolean` | executed, not an arithmetic operand |
| `text` / `String` | `text` | executed, byte-indexed and NUL-free on native |
| `void` | `nothing` | executed |
| `i8`,`i16`,`u16`,`u32`,`u64`,`f32` | no spelling | **rejected** — no consistent cross-route scalar implementation |
| `char` | no spelling | needs independent qualification |
| `any` | no spelling | erasure/recovery is rejected |

Decimal integer literals infer `i32` or `i64` by range; 32-bit hexadecimal masks
retain `i32` inference. Floating literals infer `f64`. Matrix keeps Ny's
promotion order `f64`, `i64`, `i32`, `u8` and wrapping integer arithmetic at
the resolved width. See the shared [literal-width decision](../../docs/design/NIL-0007-integer-literal-widths.md).

### Conversions

| Ny | Matrix |
| --- | --- |
| `value as i64` | `value becomes long` |
| numeric to `bool` (nonzero, NaN true) | `value becomes boolean` |
| narrowing keeps low bits | same semantics, same spelling |
| float to int requires finite in range | same; out of range fails, never clamps |
| text/scalar conversions | same spelling, **interpreted routes only** |

`converted to` is a separate word from the declaration's `as` on purpose: `as`
introduces a type in a *binding*, `converted to` performs a *cast*. Ny spells
both `as`, and that overload is the single most common source of confusion
about whether a value changed representation.

---

## 3. Compound types

| Ny | Matrix | Route state |
| --- | --- | --- |
| `array<i32>` | `array of integer` | executed for `integer`, `long`, `byte`, `boolean`, `real`, NUL-free `text` |
| `[1, 2, 3]` | `[1, 2, 3]` | executed |
| `values[0]` | `values at 0` | executed |
| `values[1] = 7` | `set values at 1 to 7` | executed |
| `values.len()` | `the length of values` | executed |
| `values.push(x)` | `append x to values` | executed |
| nested arrays | no spelling | unsupported on native |
| `ptr<T>` | `pointer to <type>` | executed for `integer`, `long`, `byte`, `boolean` |
| `allocate<i32>(2)` | `allocate values as pointer to integer sized 2` | executed |
| `deallocate(p)` | `release values` | executed |
| `ref<Struct>` | `reference to <Record>` | executed, shared object handle |
| `ref value` | `reference to value` | shared object handles for nominal struct/class locals and parameters; explicit scalar/text binding-reference call arguments have the separate contract in §6 |
| `*alias` on `ref<T>` | no spelling | rejected in Ny |
| `owned<T>` | `owned array of <type>` | executed for `integer`, `long`, `byte` |
| `borrow<T>` / `borrow_mut<T>` | `shared view of <type>` / `exclusive view of <type>` | executed |
| `(T, U)`, `(a, b)`, `let (a,b) = value` | `tuple of (T, U)`, `(a, b)`, `value (a,b) is result` | shared immutable tuple slots and typed tuple arrays on VM/native routes; record/enum/nested-array element arrays remain excluded natively |

Out-of-range typed array reads yield the element's zero value and out-of-range
writes are a no-op. Matrix keeps that, because changing it would change NIL
semantics, not Matrix's.

`len` and `push` are not library functions. They are compiler builtins
recognised on an array receiver (`type_checker.cpp` accepts `len`/`length`/
`size` and `push`/`add`) and they take positional arguments with no declared
parameter names. Matrix's named-argument call form therefore has nothing to
bind against, which is why both get dedicated spellings — `the length of
values` and `append x to values` — rather than a `with ... being ...` clause.
This is the one place where the named-argument rule meets a construct that has
no declaration, and it is worth checking for other builtins before the parser
is written.

---

## 4. Bindings and statements

| Ny | Matrix | Route state |
| --- | --- | --- |
| `let x = 42` | `value x is 42` | executed |
| `let x: i32 = 42` | `value x as integer is 42` | executed |
| `let mut c = 0` | `variable c is 0` | executed |
| `const MAX = 100` | `constant MAX is 100` | executed |
| `x = 10` | `set x to 10` | executed |
| `obj.field = v` | `set obj.field to v` | executed |
| `arr[0] = v` | `set arr at 0 to v` | executed |
| `println(v)` | `say v` | executed, **exactly one value** |
| `println("")` | `say ""` | executed |
| expression statement | `evaluate <expr>` | executed, value discarded |
| `return v` | `give v` | executed |
| `return` (void) | `give nothing` | executed |
| match/if arm result in value position | `give v` (same word, see below) | executed |
| `do` block | **no spelling** | not in the executable contract |

The split between `is` (declare and initialise) and `set ... to` (mutate an
existing binding) is load-bearing. In Ny both are `=`, and whether a line
creates storage or writes storage depends on whether `let` is present.

A non-`nothing` function that reaches its end without `give` fails, exactly as
in Ny. There is no implicit last-expression result.

---

## 5. Control flow

| Ny | Matrix |
| --- | --- |
| `if c then ... elseif d then ... else ... end if` | `when c then ... otherwise when d then ... otherwise ... end when` |
| `while c ... end while` | `repeat while c ... end repeat` |
| `for x in items ... end for` | `repeat for each x in items ... end repeat` |
| `for i in range(0, 10) ... end for` | `repeat for i from 0 to 10 ... end repeat` |
| `loop ... end loop` | `repeat forever ... end repeat` |
| `break` targeting a loop | `stop repeating` |
| `break` targeting a switch | `stop switching` |
| `break value` in a switch expression | `stop switching with value` |
| `continue` | `continue repeating` |
| `match v ... end match` | `consider v ... end consider` |
| `Pattern -> expr` (statement) | `when Pattern then <statements>` |
| `Pattern -> expr` (value) | `when Pattern give <expr>` |
| `_ ->` | `otherwise` |
| `pattern when cond ->` | `when Pattern provided cond then` |
| `Enum.Variant(binding) ->` | `when Enum.Variant holding binding then` |
| `let x = if c then a else b end if` | `value x is when c give a otherwise give b end when` |
| `match` as expression | `consider` as expression |
| `switch v ... end switch` or `switch v { ... }` | `switch v ... end switch` |
| `case literal:` | `case literal then` |
| `default:` | `otherwise` |
| `switch` as expression | `switch` as expression |

Every loop form opens with `repeat` and closes with `end repeat`, so a loop
terminator can never be mismatched to the wrong loop kind. `from 0 to 10`
visits 0 through 9, matching Ny's exclusive `range`.

`when` carries one relation — "in the case of" — in all three of its
positions: a conditional, a match arm, and a choice variant declaration. It is
reused rather than joined by a fourth word for each.

`give` carries one relation too: *this is the value leaving the construct you
are inside*. In a function body that is a return; in a value-position arm or
branch it is that arm's result. The two forms are kept apart by the grammar
rather than by convention — `then` is rejected in value position and a bare
`give` statement is rejected there too, so exactly one of the two readings
parses. The residual cost is real and is stated in
[`../README.md`](../README.md#what-when-and-give-are-allowed-to-mean): the
*target* of a `give` depends on whether an enclosing `consider` is in value
position, which is the same hazard as `return` inside a closure in other
languages.

A switch has automatic fallthrough and statement bodies in both value and
statement positions. `stop switching with value` supplies a switch result;
`give` inside its cases remains a paragraph return. Its default is mandatory
in value position. Ny's `break` targets the nearest loop or switch, while
Matrix's explicit `stop switching` can cross an intervening loop. See the
[switch reference](../../docs/language-reference/switch.md).

Guards use `provided`, not a second `when`, so an arm and its guard are
lexically distinct. A `consider` used as an expression requires a final
unguarded `otherwise` and one value-producing expression per arm, as Ny does.

---

## 6. Functions

| Ny | Matrix | Route state |
| --- | --- | --- |
| `fn f(a: i32) -> i32 ... end fn` | `f receives a as override integer returns override integer begin paragraph ... end paragraph` | executed |
| `fn f(m: text)` (void) | `f receives m as text begin paragraph ... end paragraph` | executed |
| `fn f() -> i32` | `f returns override integer begin paragraph ... end paragraph` | executed |
| `f(1, 2)` | `f with a being 1, b being 2` | executed |
| `f()` | `f with nothing` | executed |
| `mut amount: i32` / `mutable amount: i32` parameter | `mutable amount as override integer` / `mut amount as override integer`; `variable` remains an alias | callee-local mutation |
| `ref amount: i64` / `reference amount: i64` parameter | `reference amount as number` / `ref amount as number` parameter | explicit caller-binding mutation; ordinary non-generic named functions, exact scalar/text types only |
| `f(ref total)` / `f(reference total)` | `f with amount being reference to total` / `f with amount being ref to total` | mutable local/parameter target; immediate writes and forwarding, no escaping binding reference |
| `..values: i32` variadic | `receives more values as override integer` | executed for `override integer` |
| default parameter expressions | `defaulting to <literal>` | **parsed; omitted arguments rejected** |
| `fn square(x) -> i32 => x * x` | no spelling | not implemented in Ny |
| overload sets | **no spelling** | duplicate names produce duplicate NIL labels |
| named function value | `the paragraph f` | executed on interpreted routes; reference-parameter functions cannot become values |
| noncapturing lambda | `anonymous receives ... begin paragraph ... end paragraph` | executed on interpreted routes; reference parameters rejected |
| capturing closure | **no spelling** | rejected in Ny |

`returns` terminates the parameter list. Omitting `returns` means
`returns nothing`. In a parameter header `and` is accepted in place of `,`
because each item has the fixed shape `[more] [mode] <name> as <type>`:

```text
add-two receives left as number and mutable right as number returns number begin paragraph
add-two receives left as number, mutable right as number returns number begin paragraph
```

Both are the same declaration. Named call arguments lower to positional NIL
arguments in declaration order; because Ny has no overload resolution, argument
names are checked against the single declaration and are never used to select
between candidates.

`mutable` allows reassignment of the callee's parameter binding. `reference`
allows writes to the caller's mutable local or parameter, and the call must
say `reference to name`. Binding-reference parameters support `i32`, `i64`,
`u8`, `bool`, `f64`, and `text`; they reject defaults, variadics, generic
functions, methods, anonymous paragraphs, and function-value use. Reusing one
target for two arguments aliases the same binding. Writes remain visible after
a later fault, and only the referenced value can be returned or stored.
See [NIL-0019](../../docs/design/NIL-0019-binding-reference-parameters.md).

---

## 7. Records and choices

| Ny | Matrix | Route state |
| --- | --- | --- |
| `struct Point ... end struct` | `Point begin record ... end record` | executed |
| `x: i64` field | `field x as long` | executed |
| `value: i32 = 37` field default | **no spelling** | rejected in Ny |
| `Point { x: 1, y: 2 }` | `build Point with x being 1, y being 2` | executed |
| `enum E ... end enum` | `E begin choice ... end choice` | executed |
| `Some(T)` variant | `when Some holding T` | executed |
| `None` unit variant | `when None` | executed |
| `Option.Some(42)` / `Option::Some(42)` | `Option.Some holding 42` | executed |
| `enum Option<T>` | `Option of type T begin choice` | executed for concrete instantiations |
| `struct Envelope<T>` | `Envelope of type T begin record` | executed for supported field layouts |
| `type Alias = T` | `Alias means <type>` | executed |

Every construction initialises every declared field exactly once with a
compatible type, inherited fields included. Matrix has no field-default
spelling because Ny rejects field defaults, and a `field x as integer is 37`
line would read exactly like a working default.

---

## 8. Classes

| Ny | Matrix | Route state |
| --- | --- | --- |
| `class C ... end class` | `C begin class ... end class` | executed, nongeneric |
| `abstract class C` | `abstract C begin class` | executed |
| `class D inherits C` | `D inherits C begin class` | executed, multiple bases |
| `pub virtual fn read(self) -> i32` | `public virtual read returns integer begin function` | executed |
| `pub override fn read(self)` | `public overriding read returns integer begin function` | executed |
| `pub abstract fn read(self) -> i32 ... end fn` | `public abstract read returns integer` | executed, no body |
| `self.value` | `my value` | executed |
| `self: ref<Self>` receiver | `on reference` clause | executed |
| no `self` parameter (associated) | `without receiver` clause | executed |
| unmarked method (private) | no modifier | executed |
| `let base: Reader = derived` | `value base as Reader is derived` | executed, ancestor view |
| downcast | **no spelling** | rejected in Ny |
| generic classes | **no spelling** | not executed |
| trait objects | **no spelling** | not executed |

`self` is implicit and spelled `my`. Matrix synthesises the explicit first
`self` parameter that Ny requires, so the NIL `CALL_METHOD` arity is identical.
A method whose receiver must be a reference says so in a clause rather than by
declaring a parameter, because the receiver is not an ordinary argument.

Mixed-class conditional, `consider` or array element joins do not infer a
common ancestor in Matrix either: an explicit ancestor-typed value is required
first, as in Ny.

---

## 9. Contracts (traits)

| Ny | Matrix | Route state |
| --- | --- | --- |
| `trait T ... end trait` | `T begin contract ... end contract` | executed, static dispatch |
| `trait Reading<T>` | `Reading of type T begin contract` | executed |
| required `fn read(self) -> T` | `read returns T` | executed |
| `fn replace(mut self, v: T)` | `replace receives v as T on mutable receiver` | executed |
| `impl Trait for Type ... end impl` | `Type provides Trait begin implementation ... end implementation` | executed |
| `impl Type ... end impl` (inherent) | `Type begin implementation ... end implementation` | executed |
| `impl` on a non-local type | `Type begin extension ... end extension` | **new**; static dispatch, no NIL change |
| `impl<T: Bound> Envelope<T>` | `Envelope of type T meeting Bound begin implementation` | executed |
| `impl<T: A + B>` several bounds | `of type T meeting A and B` | executed |
| `T.default()` associated function | `T.default with nothing` | executed |
| default method bodies | **no spelling** | rejected in Ny |
| supertraits (`: Parent`) | **no spelling** | rejected in Ny |
| associated types | **no spelling** | rejected in Ny |

### Extensions

An extension adds paragraphs to a type the book does not declare, including a
tier-one primitive. It is the mechanism that keeps the language at five type
names while the library grows `text`'s surface, and it makes library calls
read as possession — `greeting.to-ascii with nothing` rather than
`to-ascii with source being greeting`.

It requires **no NIL change**. Resolution is static, so the call lowers to an
ordinary `CALL` of a book-qualified paragraph with the receiver as its first
argument, which is exactly how an `implementation` method already lowers. The
machinery is largely present: `type_checker.cpp` resolves an `impl` target by
scope lookup rather than by local declaration, already special-cases builtin
targets such as `array` and `ptr`, and already has a path for an out-of-module
impl method.

Excluded, because each would out-promise NIL: adding a field (a storage-layout
change), declaring a `virtual` or `abstract` member (a vtable change), and
dynamic resolution. Resolution is fail-closed: an inherent method beats an
extension; two extensions in scope offering the same name on one type are
ambiguous and reject rather than resolving by read order; and an extension
applies only where it has been `read` into scope — so the operations a page can
reach are visible in its read list, and an extension carries the authority of
the book that defines it.

Extension and implementation paragraphs follow ordinary book visibility:
write `public` on each paragraph intended for callers in another book. An
unmarked paragraph remains private to its declaring book, even when its receiver
type is public and the extension book is read by the caller. Public operations
may call private helpers within that book. The grammar has no block-level export
modifier; put `public` before the paragraph name, not the extension target.

Standard contracts are `Clone`, `Display`, `Debug`, `Default`, `Eq`, `Hash`.
`IList`, `ISet` and `IDictionary` are independent contracts and not subtraits
of `ICollection`; Matrix inherits that, including `ISet` insertion returning
`boolean` where `ICollection.add` returns `nothing`.

---

## 10. Faults

| Ny | Matrix | Route state |
| --- | --- | --- |
| `attempt ... end attempt` | `attempt ... end attempt` | executed, synchronous |
| `fault T as binding` | `caught T as binding` | executed, exact nominal match |
| `fault T as b when cond` | `caught T as b provided cond` | executed |
| `fault as token` (catch-all) | `caught anything as token` | executed, final handler only |
| `recover` | `always` | executed, cleanup, runs once |
| `raise Payload { ... }` | `raise Payload with message being "..."` | executed |
| rethrow a token | `raise token again` | executed |
| postfix `?` | **no spelling** | rejected in Ny |

Payloads are fully initialised concrete record values. Scalars, class
instances, choice values, raw pointers and erased values are not payloads, in
either language. A handler matches exact canonical record identity including
module and concrete generic arguments: `Problem of integer` does not match
`Problem of text`.

`always` is cleanup, not an extra handler. It runs on normal exit, after a
selected handler, on unmatched propagation, and through `give`,
`stop repeating` or `continue repeating`. A `give` or `raise` inside `always`
replaces the suspended exit, deliberately including suppression of an earlier
fault.

Uncaught faults exit 1. Division by zero, invalid NIL, allocation failure,
panic and explicit process exit stay fatal and are not catchable in Matrix
either.

---

## 11. Memory and ownership

| Ny | Matrix | Route state |
| --- | --- | --- |
| `let mut b = owned<i32>(64)` | `own b as array of integer sized 64` | executed for `integer`, `long`, `byte` |
| `owned_borrow(b)` | `borrow r from b` | executed, many coexisting |
| `owned_borrow_mut(b)` | `borrow w from b exclusively` | executed, exactly one |
| `owned_move(b)` | `move b into c` | executed, source invalidated |
| owners as parameters/returns/fields | **no spelling** | rejected in Ny |
| `allocate<T>(n)` / `deallocate(p)` | `allocate ... sized n` / `release p` | executed |

Ownership operations are statements rather than initialiser expressions,
because Ny requires every ownership operation to be a direct local declaration
initialiser. Making them statements means the restriction is expressed by the
grammar instead of by a later semantic error.

Lifetimes are lexical, not last-use. A buffer is freed exactly once when its
current owner's scope exits, including through `give`, `stop repeating`,
`continue repeating` and fault unwinding. Control-flow checking is
conservative: if either branch moves an owner, later use is rejected.

---

## 12. Annotations, context and interop

| Ny | Matrix | Route state |
| --- | --- | --- |
| `@ai_context("...")` | `context "..."` | retained in NIL; inspectable |
| `@intrinsic("console.writeln")` | `note intrinsic "console.writeln"` | provider-specific |
| `@inline` | `note inline` | flag bit 0 |
| `@interop(...)` | `note interop ...` | **experimental**, requires a linked provider |
| `test` blocks | `name begin test ... end test` | flag bit 1 |
| `asm "x86_64", "..." (in) -> out end asm` | `assembly for x86_64 is "..." using in giving out` | native x86_64 only |

Annotations support scalar text, boolean, integer, floating-point, and symbol
values. Matrix preserves raw-string behavior inside `@` arguments. Named
paragraphs, records, classes, choices, and supported methods can carry them;
unsupported attachments are rejected. See the
[shared reference](../../docs/language-reference/annotations.md).

An annotation is not an implementation in Matrix either. `context` is promoted
to a first-class statement rather than a generic annotation because
self-description for agents is the reason Matrix exists; it lowers to exactly
`@ai_context` and is disclosed by `--inspect-context`. Context text is
unauthenticated data, never trusted instruction, and secrets do not belong in
it.

Inline assembly keeps Ny's bounded profile unchanged: x86_64, register-only,
`long` operands, at most eight inputs and one output, from the approved
instruction list. Interpreted routes reject it rather than emulating it.

---

## 13. Concurrency and time

| Ny | Matrix | Route state |
| --- | --- | --- |
| `parallel_map(worker, inputs)` | `apply worker across inputs` | executed, bounded fork/join |
| `TIME_NOW` | `now in milliseconds` | executed |
| `TIME_SLEEP` | `sleep for n milliseconds` | executed |
| async, channels, task handles | **no spelling** | not implemented |

`apply ... across ...` keeps the whole Ny contract: a statically resolved,
nongeneric worker with one scalar parameter and one scalar result, an input
array of the parameter type, deterministic result order, at most 1,048,576
inputs, and a worker call graph that cannot touch globals, pointers, text,
arrays, I/O, clocks or faults. A worker failure is a runtime error, not a
catchable fault.

---

## 14. Areas, bundles and visibility

| Ny | Matrix | Route state |
| --- | --- | --- |
| `module math.utils` | `area math.utils` | executed |
| `package my.app` | `bundle my.app` | executed |
| `import ny.lang.io.file` | `link ny.lang.io.file` | executed |
| `import math.utils.{add, subtract}` | `link add, subtract from math.utils` | executed |
| `import math.utils as mu` / `namespace mu = ...` | `link math.utils as mu` | executed |
| `use math.utils` | `link everything from math.utils` | executed |
| `pub use other.{A, B}` re-export | `share A, B from other` | executed |
| `pub` | `public` | executed |
| (none) | (none) — private to the area | executed |
| `pub(crate)` | **no spelling** | parsed only; no release guarantee |
| `pub let mut level: i32 = 1` | `public variable level as integer is 1` | executed, `module::name` identity |
| `settings::level` | `settings::level` | executed |

Area-level initialisers run in declaration order, not lexical name order. A
typed forward reference type-checks and still fails at runtime if it reads
before initialisation; Matrix does not invent a zero. Arbitrary top-level
statements are not an initialisation mechanism in either language. Aggregate
globals remain interpreter-only.

`::` is kept rather than naturalised. A storage identity is exactly
`module::name` in NIL, and cross-language identity is the entire point of the
exercise; an English paraphrase of a linkage name would be a liability.

---

## 15. Deliberately unspelled

Collected for review. Each is syntax Matrix could plausibly have and must not,
because NIL cannot execute it. The correct time to add each spelling is when
its route evidence lands.

Field default initialisers · overload sets · arrow-body functions ·
capturing closures · native indirect calls · dynamic trait objects · generic
class execution · generic method execution · first-class bound methods ·
downcasts · reference and container covariance · automatic destructors ·
postfix `?` · default trait method bodies · supertraits · associated types ·
scalar `ref` creation · nested array elements · `i8`/`i16`/`u16`/`u32`/`u64`/`f32`
arithmetic · `any` erasure · standalone range values · inclusive ranges ·
`do` blocks as expressions · async, channels and task handles · cross-thread
fault propagation · foreign exception interoperability · subprocess execution ·
`http.send` and network transports · operator-trait dispatch · extension blocks
