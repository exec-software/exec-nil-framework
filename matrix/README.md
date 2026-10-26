# Matrix

Matrix is a second language front end for the NIL runtime. Ny and Matrix are
different surface syntaxes over the same semantic core, the same type checker,
and the same NIL bytecode, in the same way that Visual Basic, C# and F# are
different surfaces over one CLR.

Ny is designed for humans and AI and supplies the infrastructure. Matrix is a
low-level systems language whose surface is **natural-language word order**, so
that an AI agent writing code spends its budget on the program rather than on
balancing punctuation.

The front end is implemented through stage 6 of
[`docs/front-end-plan.md`](docs/front-end-plan.md): a `.matrix` page lexes,
parses and lowers through the shared AST, type checker and NIL generator, and
is covered by the `matrix.lexer`, `matrix.execution` and `matrix.routes.*`
conformance suites in [`../tests/matrix/`](../tests/matrix/). The documents
below are the proposal that corpus implements, so parts of them still
describe stages that are not built; the status table is the authority on
which.

## Status

| Item | State |
| --- | --- |
| Surface syntax proposal | Drafted here; not frozen |
| Ny feature inventory and mapping | [`docs/ny-surface-inventory.md`](docs/ny-surface-inventory.md) |
| Grammar sketch | [`docs/grammar.ebnf`](docs/grammar.ebnf) |
| Front-end integration plan | [`docs/front-end-plan.md`](docs/front-end-plan.md) |
| Next-version findings and validation notes | [`docs/next-version-findings.md`](docs/next-version-findings.md) — confirmed limitations, design friction, fixes, and acceptance checks |
| Sample programs | [`samples/`](samples/) |
| Shell profile | [NIL-0018 proposal](../docs/design/NIL-0018-matrix-shell-profile.md) and ordinary hosted syntax example; submission parsing and restricted execution are not implemented |
| Lexer, parser, lowering | **Implemented through stage 6** — see [`docs/front-end-plan.md`](docs/front-end-plan.md) for the stage-by-stage status and exit evidence |

Source files use the `.matrix` extension, or `.mat` as the short form. Both
select the Matrix front end in the compiler driver. Both feed the shared compilation pipeline:

```text
Matrix source (.matrix/.mat) -> Matrix front end -> shared AST -> type checker
  -> NIL (.nil) -> nilinterpreter
  -> native backend -> host executable
```

## Design principles

These are the reasons Matrix looks the way it does. They are deliberate
trade-offs against brevity, and brevity is the thing being traded away.

**1. Words, not punctuation.** A model that emits `}` one line early produces a
file that still parses and means something else. A model that emits
`end function` one line early produces a file that fails to parse at a named
construct. Matrix keeps `[` `]` for array literals, `(` `)` for grouping, `,`
for list separation, `.` for member access and `::` for path qualification.
Everything else is a word.

**2. One canonical phrasing per construct.** The temptation with a
natural-language surface is to accept synonyms — `returns`, `gives`, `yields`.
Synonyms make the grammar ambiguous, make diffs unstable, and make two agents
produce two spellings of one program. Matrix's naturalness comes from word
*order*, not from vocabulary breadth. There is one spelling for each thing.

**3. A word is reused when the relation is the same, not when the word is
free.** `.` is already multi-context in Ny — member access, decimal point,
area path — and that works because all three are *selection*. Matrix applies
the same test rather than minting a keyword per construct:

- `when` means "in the case of" in a conditional, a match arm, **and** a
  choice variant declaration. One relation, three positions, no `case`
  keyword.
- `give` means "this is the value leaving the construct you are inside" — a
  function body, a value-position arm, a value-position branch.
- `otherwise` means "in every remaining case" for `else` and for the wildcard
  arm.

The test has teeth: it is what rules *out* spelling a choice payload
`give long`. See
[what `when` and `give` are allowed to mean](#what-when-and-give-are-allowed-to-mean).

**4. A name is one token, however many words it has.** A hyphen joins words
within a name. Unqualified references to declarations on the same page fold
ASCII case and separators, so `frame-kind`, `FRAME-KIND` and `frame_kind`
resolve to that page's declared spelling. Emitted NIL symbols preserve the
declaration's case and replace `-` with `_`. Qualified names, imported names
not declared on the page, and member names preserve their written case; use
the declaration's spelling for these references. Subtraction uses `minus`.
Negation reads `negative offset`, with `minus offset` retained as an alias.
An adjacent sign is also accepted on numeric literals: `-1.6`, `-1.6e-3`,
and `-42`. The sign must touch the digits; `total - 1` is not subtraction.

**5. Every block closes with its own name.** `begin function` closes with
`end function`; `begin record` closes with `end record`. Mis-nesting is a
diagnostic at the exact token, not a cascade. Truncated output is detectable.

**6. The declaration reads as a sentence, subject first.** Every declaration
has the same shape:

```text
<modifiers> <name> <clauses...> begin <kind>
    ...
end <kind>
```

This is one pattern to learn and one pattern to generate, for functions,
records, choices, classes, contracts, implementations and tests.

**7. Named arguments, lowered to positional.** Models mis-order positional
arguments. `with left being 1, right being 2` cannot be mis-ordered. It lowers
to an ordinary positional NIL call after the checker resolves the callable's
parameter names. Calls through function values without a checked parameter-name
contract use positional arguments; see [the guide](MATRIX-GUIDE.md#6-expressions).

**8. The surface must not out-promise NIL.** This is the hard rule. Ny's
documentation is scrupulous about the difference between syntax the parser
accepts and behaviour a route actually executes. Matrix inherits that rule:
Matrix does not get surface syntax for closures with captures, dynamic
trait objects, generic classes, postfix `?` or overload sets, because NIL
cannot execute them today. Matrix gains those spellings when the runtime gains
the capability, not before.

## Why this language, beyond syntax

Matrix is intended to become a language for both AI agents and humans, including
in embedded and OS-level settings, with kernel-enforced authority and a chain of
trust starting at the boot loader. Anomalous selects an unprivileged native
interpreter vessel rather than a kernel-resident executor. These authority and
sandboxing capabilities are a design goal, not a property of the current desktop
toolchain. Today Matrix programs use
both the interpreter and native compilation routes, subject to the shared
runtime's supported-feature contracts.

The design rule is **full power, zero ambient authority**. Matrix keeps raw
pointers, inline assembly, foreign calls and syscalls; nothing is removed to
make it safe, because a systems language that cannot touch hardware is not one,
and a sandbox built by amputation protects nothing. What is removed is
*ambient* authority: power that comes from merely being code. Authority becomes
declared, named, verified from the artifact rather than from the compiler's
word, attenuable, and refusable by the kernel before the first instruction.

Ny already ships this mechanism in narrow form —
`concurrency_contract.cpp` verifies `parallel_map` worker call graphs against a
default-deny opcode allowlist across the full transitive closure, on NIL rather
than on source, so that `--no-typecheck` and hand-written NIL cannot escalate.
The capability system generalises that verifier rather than replacing it.

See [`docs/authority-and-sandboxing.md`](docs/authority-and-sandboxing.md). The
`requires` clause is reserved in the grammar now so that adding it is additive;
the base surface does not depend on it.

## Cross-language compatibility

Compatibility comes from both front ends producing the same NIL, carrying the
same `module::name` storage identities, the same struct and enum schemas, and
the same class ancestry records. A Matrix `book` *is* a NIL module. A Ny
`import` of it is an ordinary module import.

Source imports resolve `.ny`, `.matrix`, and `.mat` pages. Binary NIL libraries
(v2/v4, flags 1) retain each embedded source's path and contents. The importer
uses that path's extension to select the Ny or Matrix parser, then re-checks
and regenerates the imported bodies as described in
[the NIL format contract](../docs/advanced/nil-format.md). Both front ends are
already in the compiler process. Matrix libraries need neither generated Ny
stubs nor a NIL format version bump for this bootstrap import path. They do
require a compiler that includes the Matrix frontend; an older Ny-only compiler
does not gain that capability from the artifact alone.

The SDK now installs, packages, and validates both language surfaces. The
[SDK Matrix source tests](../tests/sdk_matrix_sources_tests.py) package a mixed
Ny/Matrix module and a Matrix-only module, then execute Ny and Matrix consumers
against isolated source-only and binary-only libraries on the VM and native
backend. This verifies those fixtures, rather than certifying every standard
library module or removing the current embedded-source bootstrap dependency.
Naming and remaining tooling boundaries are tracked in the
[next-version findings](docs/next-version-findings.md).

## Deliberate changes from the first draft

The initial sketch is honoured where it can be. Four things changed, each for a
stated reason:

| Draft | Matrix | Reason |
| --- | --- | --- |
| `receives tuple of int and int` | `receives values as tuple of (number, number)` | Tuples are explicit structural values. Commas delimit their element types; parameter lists remain distinct from tuples. |
| `int`, `integer` both used | `integer` only | One canonical spelling (principle 2). `int` is not an accepted alias. |
| `and` separating list items everywhere | `,` separates list items; `and` is boolean conjunction. `and` is additionally permitted between **parameter declarations** only | `and` as both a list separator and a boolean operator is ambiguous wherever a list item can be an arbitrary expression. In a parameter header each item has the fixed shape `<name> as <type>`, which keeps `and` safe there, so the draft's reading survives in the place it reads best. |
| `returns nothing` after `and` | `returns` terminates the parameter list with no `and` | Removes the ambiguity between "another parameter follows" and "the return type follows". |
| two comment forms: `!!!` plus text, and a `!!!`-delimited block | the line form only; a bare `!!!` is an empty comment line | **The two forms are mutually ambiguous.** A bare `!!!` written as a blank line inside a run of comments is indistinguishable from a block delimiter. This was found by writing the showcase sample, whose header alone contains 25 such lines. Ny has line comments only for the same reason. |

### What `when` and `give` are allowed to mean

Two reuse decisions, and one rejection, worked through because they set the
precedent for every future one.

**`case` is gone; variants are declared with `when`. Adopted.** Declaring a
case and selecting a case are the same relation, so they share a word:

```text
FrameKind begin choice
    when Sample holding long
end choice

consider kind
    when FrameKind.Sample holding value then ...
end consider
```

This removes a reserved word at zero ambiguity cost — a choice body admits
nothing but variants, so `when` there can mean nothing else.

**Value-position arms and branches say `give`. Adopted, and it buys more than
brevity.** Ny requires "exactly one value-producing expression per arm", and
with `then` that rule has to be a semantic check after a block is parsed. With
`give` it is a parse rule:

```text
value word as text is consider code
    when 1 give "one"
    otherwise give "other"
end consider
```

`then` is rejected in value position and a bare `give` statement is rejected
there, so exactly one of the two readings parses and they cannot be silently
mixed.

The honest cost: a `give` inside a *statement* arm returns from the enclosing
function, while a `give` in a *value* arm supplies the arm's result. Same
word, two targets, disambiguated by whether an enclosing `consider` is in
value position. That is the same footgun as `return` inside a closure. It is
contained — the parser rejects the wrong form rather than miscompiling it —
but it is the one place where the reuse principle costs something, and it is
the thing to overrule if it reads badly in practice.

**A choice payload stays `holding`, not `give`. Rejected, and this is the one
place I'd push back.** Three reasons:

1. `give` in `give left plus right` is an action with a value operand. `give`
   in `when Sample give long` would be a *type annotation*. Different part of
   speech, so the reuse test fails — unlike `.`, whose three meanings are all
   selection.
2. It saves no keyword. `holding` is still required in the pattern that
   destructures the payload, so the vocabulary is the same size either way —
   `give long` only moves which side spells it differently.
3. It breaks the mirror. `when Sample holding long` declares the payload and
   `when FrameKind.Sample holding value` binds it; the declaration and its
   destructuring read as the same shape. With `give long` the two sides stop
   matching, which is exactly the kind of correspondence an agent generating a
   match from a declaration benefits from.

I changed your `FrameKind` edit back to `holding` and left the reasoning in a
comment at that exact site, so reverting it is one edit if you disagree.

### Minimal keywords, maximum context

One word may carry more than one meaning when position disambiguates it, and
when the meanings are the same relation applied to different things. `forever`
after `repeat` is an unbounded loop; `forever` as a declaration modifier is
unbounded lifetime — what other languages call `static`. One relation, "without
end", applied to iteration or to storage.

Older languages avoided this because a keyword that changed meaning with
context was genuinely hostile to a reader scanning punctuation and placement.
Natural language has no such problem: it changes meaning by position
constantly, and a reader follows without effort. That is affordable here
*because* the keyword set is a small closed subset — the thing that makes
Matrix not-quite-natural-language is the same thing that makes positional
meaning safe.

Two guards keep this from becoming mush:

- **It is polysemy, not contextual keywords.** `forever` is always a keyword
  and never an identifier, so the lexer needs no feedback from the parser. Only
  the parser assigns meaning, from position. The reserved-word rule below is
  untouched.
- **The meanings must be the same relation.** This is the same test that admits
  `when` for conditionals, match arms and choice variants, and that rules out
  `give` for a choice payload type. A word with two genuinely unrelated
  meanings gets two words.

`becomes` is the cast, replacing `converted to`: one word instead of two, and
it is literally what a cast does — the value becomes that type. It was
considered for the extension block and `extension` kept that slot, since
`begin extension` names the construct and `becomes` names an action.

### Case folding and hyphens

Both adopted. Three details had to be pinned down rather than assumed, and all
three are forced by Ny's existing contract rather than chosen.

**The hyphen cannot survive into NIL.** Ny identifiers are
`letter { letter | digit | '_' }` — no hyphen. A Matrix type declared
`frame-kind` must reach NIL as `frame_kind`, or no Ny program could name it,
which would defeat the entire exercise. So `-` is a source-level joiner that
lowers to `_`.

**Case cannot fold on the wire.** `faults.md` requires exact nominal record
identity where "case and module punctuation are significant", and
`classes.md` requires class names to "preserve exact module and case
identity". Folding case in NIL would break fault matching and class ancestry
across the language boundary. So the **declaration site** fixes the emitted
spelling. Unqualified references resolve by folded lookup against declarations
on the same page. Qualified references, imports from another page, and member
names retain their written case, even when both pages use Matrix. An Ny
program also names a Matrix declaration by its exact emitted spelling, with
hyphens replaced by underscores. Case folding at local lookup does not change
NIL symbol identity.

**Folding is ASCII-only.** Full Unicode case folding is locale-dependent
(Turkish dotless i, German sharp s). Two identifiers that fold together while
looking different is a confusable-name hazard, not a convenience, so a
non-ASCII letter compares byte-exact.

Two smaller consequences worth naming:

- **A hyphen must be followed by a letter.** `total-1` is then a lex error
  rather than an undefined identifier, so the C habit of writing `total - 1`
  fails at the hyphen with a message that names the actual rule. `-kind`,
  `kind-` and `frame--kind` are rejected for the same reason.
- **`SCREAMING_CASE` stops meaning anything.** Once case folds, `MAX-PAYLOAD`
  and `max-payload` are one name, so capitals cannot mark a constant. The
  `constant` keyword does that; capitals become a human signal with no
  semantic weight.

**This needs a formatter, and that is not optional.** Case-insensitive
languages historically drift into inconsistent spelling because nothing
enforces agreement — `frame-kind` here, `FrameKind` there, and noisy diffs
between two agents editing one file. A formatter that rewrites every reference
to its declaration's spelling and every keyword to lowercase turns the feature
into what it is meant to be: the agent cannot get it wrong, *and* the file
stays consistent. Without one, folding trades a class of errors for a class of
diff noise.

**It also worsens the reserved-word tax below.** A collision can no longer be
escaped by capitalising, because `Value` *is* `value`.

### Reserved words, and why the cost is smaller than it looks

Keywords are **fully reserved**. A variable can never be a keyword, and that
is permanent rather than a stage we grow out of. It is also not a wart — it is
load-bearing. Older languages recovered structure from punctuation and
placement: braces, semicolons, significant indentation. Matrix removed those,
so the reserved words *are* the invisible structure the parser anchors on. It
is also why Matrix cannot be pure natural language: English offers "hello",
"hi", "what's up", "yo", and a German programmer adds "guten tag" — a compiler
needs one spelling per construct, so Matrix is a strict subset of natural
language rather than natural language itself.

**But a keyword never contains a separator, so a spelling that contains one is
unconditionally a name.** `read` is the import keyword; `read-file`,
`read-path` and `read_regular_bytes` are ordinary names. The reserved set costs
its single word, not the whole family of names built from it — which is what
makes a 133-word vocabulary affordable in the first place.

The test is on the *spelling*, not the folded lookup key, and that distinction
is the implementation's sharpest edge. The key has its separators removed, so
checking the key would turn `re-ad` into the keyword `read`. Both hyphen and
underscore exempt, because they already fold together for identity; exempting
only the hyphen would leave `a_s` collapsing onto `as`.

The remaining consequence is the desirable one: a name whose folded key happens
to equal a keyword can only be referenced through a spelling that keeps a
separator, because the separator-free spelling is the keyword. Renaming is
still the better answer when a bare keyword is wanted; the backtick escape is
a last resort.

### The naming tax, measured

A symbol-based language reserves `if`, `for`, `return`. A natural-language
surface reserves `value`, `variable`, `constant`, `text`, `long`, `more`,
`at`, `is`, `with`, `of`, `my` — words that are also the most natural names
for fields and parameters. Writing the samples hit this immediately: `field
value as long`, `receives value as text` and `my value` all read perfectly and
all collide.

The samples resolve each remaining collision by renaming (`level`, `amount`,
`replacement`, `line`, `word`, `read-level`). With the separator rule above,
the genuine collisions are only the cases where a *bare*, separator-free
keyword is wanted as a name — a much smaller set than it first appears.

Contextual keywords were considered and rejected: the lexer would need parser
feedback or a two-pass scan, diagnostics get harder to word, and the structural
anchoring described above is exactly what would be given up.

The surviving ambiguity is documented rather than hidden: in a parameter
header, `and` requires two tokens of lookahead (`and <name> as`) to distinguish
a further parameter from a boolean operand inside a parameter default. Matrix
therefore restricts parameter defaults to literal atoms. Since Ny retains
default expressions in the syntax tree but **rejects omitted arguments at the
call site**, this costs nothing today.

## Open decisions

These need your call; the samples pick an answer so they can be read, and the
answer is marked here as provisional.

1. **Extension.** `.matrix` (chosen) or `.matrix`.
2. **Package keyword.** `area` is the module declaration, from your draft.
   Packages are spelled `bundle`. Alternatives: `kit`, `collection`.
3. **Zero-argument calls.** Matrix spells them `read_value with nothing`, so a
   call is always syntactically a call and never confusable with an identifier.
   The alternative is bare `read_value` plus a resolution rule, or `()`.
   Regularity was chosen over brevity; this is the most verbose consequence of
   that choice and the most reasonable one to revisit.
4. **Word operators.** `plus`, `times`, `is at least` with conventional
   precedence and `(` `)` for grouping. Numeric-heavy code is wordier than Ny.
   The alternative is symbolic arithmetic with word logic.
5. **`recover` renamed to `always`.** Ny's `recover` is cleanup with `finally`
   semantics, not a handler; the Ny docs say so explicitly. `always` says it in
   the surface. This makes the two languages read differently about one
   identical NIL construct, which is a cost worth naming.

## If you just want to write Matrix

Read [`MATRIX-GUIDE.md`](MATRIX-GUIDE.md). It is self-contained — you should
not need the compiler or these design documents to write a working program —
and every construct in it is compiled and asserted by
`tests/matrix/execution/guide-constructs.matrix`. A copy lives at
`Execute/Resources/Execute.Resource.Language.Matrix.md`.

The documents below are the design record: why the surface is shaped as it is,
what it deliberately refuses, and how the front end is built.

## Reading order

1. [`docs/ny-surface-inventory.md`](docs/ny-surface-inventory.md) — everything
   Ny can do, what Matrix spells it as, and what deliberately has no spelling.
2. [`samples/`](samples/) — one file per feature area, in order.
3. [`docs/grammar.ebnf`](docs/grammar.ebnf) — the grammar sketch.
4. [`docs/front-end-plan.md`](docs/front-end-plan.md) — how this gets built.
