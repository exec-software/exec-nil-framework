# Matrix front-end plan

How the Matrix surface language joins the NIL runtime without forking its
semantics. Stages 1–6 are implemented; the sections below record their design,
exit evidence, and the remaining cross-language and tooling work. The
[Matrix overview](../README.md#status) and
[next-version findings](next-version-findings.md) describe the current scope.

## The architectural choice

Two ways to add a front end:

**(a) A separate compiler that emits NIL.** `matrixc` gets its own lexer,
parser, type checker and NIL generator. Cross-compatibility then rests on two
independent implementations agreeing about scalar promotion, class ancestry,
fault matching, ownership checking, native layout rejection and a dozen other
contracts that Ny's own documentation describes as narrow and evidence-scoped.
Every divergence is a silent miscompile across a language boundary.

**(b) A second surface over the shared semantic core.** The Matrix front end
is a lexer and parser that produce the existing `nilcompiler/src/ast.hpp`
nodes, and then nothing else happens differently: `type_checker.cpp`,
`codegen.cpp`, `llvm_backend.cpp`, `ownership_check.cpp`,
`class_contract.cpp`, `concurrency_contract.cpp` and `owned_contract.cpp` all
run unchanged.

**(b) is the design.** It is also what makes the Visual Basic / C# comparison
accurate rather than aspirational: those languages share a semantic core, not
merely a bytecode. The consequences are worth stating plainly:

- Matrix cannot have a semantic feature Ny lacks, because there is no place to
  put it. A new capability is added to the core and then gains a spelling in
  each surface. This is a feature, not a limitation — it is the mechanism that
  keeps the two languages from drifting.
- Matrix's diagnostics come from the shared checker and will initially speak
  about Ny-shaped constructs. The source-span plumbing has to carry Matrix
  spans and a Matrix message catalogue has to exist, or error quality becomes
  the reason nobody uses the language.
- Matrix's surface *restrictions* (ownership operations as statements, no
  field defaults) are enforced in the parser, so they are parse
  errors with precise locations rather than semantic errors discovered later.

## Where the code goes

```text
nilcompiler/src/
  matrix_lexer.cpp     .matrix tokens; keyword table; !!! comment forms
  matrix_lexer.hpp
  matrix_parser.cpp    .matrix -> ast.hpp nodes; desugaring lives here
  matrix_parser.hpp
  matrix_diagnostics.cpp  Matrix-worded messages over shared checker results
```

One compiler binary, two front ends, selected by extension. Both
`nilcompiler program.matrix --output program.nil` and
`nilcompiler program.mat --output program.nil` select Matrix. The analysis
command also accepts unsaved text through `--source-stdin`, using its logical
source filename to select the front end. There is no `--language` override in
the current driver. A separate `matrixc` binary would duplicate the driver,
SDK discovery, bundle writer, context inspector, and native pipeline.

## Desugarings the parser performs

These are the whole of Matrix's semantic content. Each is a syntactic
transformation into an existing AST node; none of them invents behaviour.

| Matrix | Lowered to |
| --- | --- |
| `my value` | `self.value`, with a synthesised explicit first `self` parameter on the enclosing method |
| `on reference` | `self: ref<Self>` receiver |
| `on mutable receiver` | `mut self` receiver |
| `f with b being 2, a being 1` | positional `f(1, 2)`, reordered against the single declaration |
| `f with nothing` | `f()` |
| `build P with x being 1, y being 2` | `P { x: 1, y: 2 }` |
| `value x is e` / `variable x is e` / `constant X is e` | `let` / `let mut` / `const` |
| `set p to e` | assignment |
| `values at i` | `values[i]` |
| `say e` | `println(e)` |
| `give e` | `return e` |
| `evaluate e` | expression statement |
| `e becomes T` | `e as T` |
| `repeat for i from a to b` | `for i in range(a, b)` |
| `repeat forever` | `loop` |
| `stop repeating` / `continue repeating` | `break` / `continue` |
| `when`/`otherwise when`/`otherwise` | `if`/`elseif`/`else` |
| `consider` / `when P holding b provided g then` | `match` / `P(b) when g ->` |
| `caught T as b provided g` | `fault T as b when g` |
| `caught anything as t` | `fault as t` |
| `always` | `recover` |
| `raise t again` | `raise t` on a catch-all token |
| `own b as array of T sized n` | `let mut b = owned<T>(n)` |
| `borrow v from b [exclusively]` | `let v = owned_borrow[_mut](b)` |
| `move b into c` | `let c = owned_move(b)` |
| `allocate p as pointer to T sized n` | `let p = allocate<T>(n)` |
| `release p` | `deallocate(p)` |
| `apply w across xs` | `parallel_map(w, xs)` |
| `now in milliseconds` / `sleep for n milliseconds` | the `TIME_NOW` / `TIME_SLEEP` wrappers |
| `context "..."` | `@ai_context("...")` |
| `note <name> <args>` | `@<name>(<args>)` |
| `area` / `bundle` / `link` / `share` | `module` / `package` / `import` / `pub use` |
| `frame-kind` (declaration) | NIL symbol `frame_kind` — `-` rewritten to `_`, exact declared case preserved |
| `FRAME-KIND`, `frame_kind`, `framekind` (reference) | resolved by folded lookup key to that one declaration's symbol |
| `Alias means T` | `type Alias = T` |
| `X begin test` | `test X` |
| `assembly for x86_64 is "..." using i giving o` | `asm "x86_64", "..." (i) -> o end asm` |

Named-argument labels remain on the shared AST until the checker selects the
actual lexical, imported or member callable. The checker then orders arguments
against that declaration's parameters; emitted NIL remains positional. Labels
do not select overloads. Immutable local paragraph values can retain a proven
parameter-name contract from a declaration, anonymous paragraph or another such
local. Function types themselves currently contain types without parameter
names, so callbacks without this provenance require positional arguments.

## Cross-language consumption

Source-level linking works as soon as the front end does, because both
languages produce one resolved module set. A `.matrix` file linking a `.ny` area
and a `.ny` file importing a `.matrix` area are the same operation to the driver.

Binary libraries are the real work. Per
[the NIL format contract](../../docs/advanced/nil-format.md), v2/v4 libraries
carry section 6 — `source_count`, path/content string pairs, dependency names —
and the current bootstrap importer "re-checks the interface and regenerates
imported bodies". That re-check is a parse, so it is language-specific.

- **Stage 1, no format change.** A Matrix library embeds a generated,
  declaration-only **Ny** interface stub. The generator already exists in
  spirit: the AST is shared, so emitting Ny declaration syntax from it is a
  printer, not a translator. Today's `nilcompiler` consumes the result with no
  changes at all. This is the cheapest path to a demonstrable
  `Matrix library -> Ny application` link.
- **Stage 2, format change.** Section 6 gains a per-source language tag, both
  front ends live in one process, and the importer dispatches on the tag. This
  is a version bump and wants its own `notebook/decisions/` record, including
  the old-version fixtures and round-trip tests that NIL-0003 requires of
  a format change.

Neither stage changes the executable sections. Class ancestry (section 7),
struct and enum schemas (section 1), function annotations and flags
(section 2) and `module::name` global identities are already language-neutral;
that is why this works at all.

## Proposed sequence

Each stage has an exit condition, in the spirit of the repository's existing
route-evidence discipline. "It parses" is not an exit condition.

### Status as of 2026-10-14

| Stage | State |
| --- | --- |
| 1. Freeze the surface | Settled for the areas the samples cover; open decisions listed in [`../README.md`](../README.md) |
| 2. Lexer, folding, reserved words | **Done.** `matrix_lexer.{hpp,cpp}`, 40 checks in `matrix.lexer` |
| 3. Parser, scalar subset | **Done.** `matrix_parser.{hpp,cpp}` |
| 4. Aggregates | **Done.** 11 fixtures in `matrix.execution`, 2 schema comparisons |
| 5. Classes and contracts | **Done.** 14 fixtures, 2 schema comparisons, 3 diagnostics |
| 6. Faults, ownership, parallelism, assembly | **Done.** 4 execution fixtures, 4 fixtures on all six routes, 1 schema comparison, 1 diagnostic, the assembly route split |
| 7. Cross-language linking | **Done.** 28 fixtures; a Matrix/Ny/Matrix chain links and runs |
| 8. Tooling | Not started |

Stage 3 lowers books, libraries, all four `read` forms, `cite`, paragraphs with
named and variadic arguments, bindings, `set`, `say`, `give`, `evaluate`,
`append`, `when` (statement and value position), every `repeat` form,
`stop`/`continue repeating`, word operators with their precedence, `becomes`
casts, `the length of`, indexing with `at`, type aliases, annotations and book
globals with `::` qualification.

Stage 4 adds records with `field` members, choices whose variants are declared
with `when`, generic records and choices, construction with `build`, variant
construction with `holding`, `consider` in both statement and value position
with `provided` guards and literal or variant patterns, raw pointers via
`allocate`/`release`, `reference to`, and declaration-site name resolution so
that every case and separator spelling of a name reaches the one declared NIL
symbol.

Its exit condition was **NIL schema equality against a hand-written Ny twin**,
not matching output, and that is what `matrix.execution` now checks: for each
`<name>.matrix` with a `<name>-twin.ny`, the struct and enum records of the two
artifacts must be byte-identical. The check is negative-tested — changing one
field from `i64` to `i32` in the twin fails it with a diff.

That check immediately earned itself. Ny normalises `module a.b` to `a::b`, and
Matrix was emitting `a.b`, so **every schema name differed while every printed
value matched**. Output comparison alone would have shipped a language that
could never interoperate.

Three more bugs came from the same exercise:

- In expression position a dot is member access, but `parse_primary` was
  swallowing dots into one dotted identifier, so `job-state.Queued` never
  became the `member_access(identifier, variant)` shape codegen requires. A
  `::` anywhere in the path is the discriminator for a book-qualified
  reference.
- `codegen.cpp` requires a match-expression arm body to be exactly one
  `expr_stmt`; a block holding a `return` would have returned from the
  enclosing paragraph instead of supplying the arm's value.
- Case and separator folding lived only in the lexer's lookup key while the
  parser emitted the written spelling, so `READ-SENSOR` reached the checker as
  `READ_SENSOR`. The documented rule — the declaration site fixes the symbol,
  references fold against it — needed a pre-scan to implement, because a
  reference may precede its declaration. A name this page never declares falls
  back to its written spelling, which is the right answer for the standard
  library: `read-regular-bytes` becomes `read_regular_bytes`.

Stage 5 adds classes with `inherits` and multiple bases, `abstract` classes
and members, `virtual` and `replaces`, contracts as signature-only
declarations, implementations with and without a named contract, extensions on
a type the book does not declare (including a tier-one primitive, whose name is
a keyword), the four receiver clauses, and `my` as the receiver — synthesised
into the explicit first `self` parameter Ny requires, so the NIL
`CALL_METHOD` arity is identical.

Its exit condition was **NIL class ancestry equality**, and
`matrix.execution` now compares struct, enum *and* class records against the
Ny twin. The class records match exactly, including the abstract flag, each
base, every method's implementation symbol, receiver type, return type and the
virtual/abstract flag bits, and the empty implementation symbol an abstract
method carries.

The comparison also gained a guard against passing vacuously: a page with a
twin that declares no struct, enum or class now fails rather than trivially
agreeing.

Case folding turned out to have a style consequence worth stating plainly, and
it bit three times in one sample before being caught: **a value cannot be
named after its own type in different case.** `reader`/`Reader`,
`tagged`/`Tagged` and `point`/`Point` are each one name, so the extremely
common convention of naming a local after its type is unavailable. Left
undiagnosed it is genuinely baffling — the reference resolves to the type, and
the failure surfaces much later as a NIL fault-contract rejection on a method
call emitted with no receiver. The parser now reports it at the offending name
with the rule and a suggested rename, and
`tests/matrix/diagnostics/type-name-collision.matrix` keeps it honest.

Two more findings from running the corpus:

- An extension may target a tier-one type, whose name is a **keyword**
  (`text begin extension`), and may apply concrete type arguments
  (`envelope of number begin extension`). Both needed explicit handling, since
  a declaration otherwise requires a name and reads `of` as a generic clause.
- Sample 06 contained a real error of its own: `Reader.label` and
  `Tagged.label` were both inherited by one class, which is an ambiguous
  inherited method. Ny rejects it rather than resolving by base order, exactly
  as the sample's own comment claimed it would.

Stage 6 adds `attempt` with typed `caught` handlers, `provided` guards, the
`caught anything` catch-all and `raise ... again`, `always` cleanup, `raise`
with direct payload construction, owned buffers (`own`, `borrow`,
`borrow ... exclusively`, `move ... into`), `apply ... across` for bounded
fork/join, `now in milliseconds` and `sleep for ... milliseconds` through
intrinsics synthesised on first use, inline `assembly`, `begin test` blocks,
bodyless `note intrinsic` declarations, and anonymous paragraphs.

`matrix/samples/15-telemetry-relay.matrix` — the 1100-line showcase — now
compiles and runs end to end. Getting there turned up five things, and three of
them are limitations of the runtime rather than of the front end. Each was
confirmed by writing the equivalent Ny by hand and watching it fail the same
way, which is the only way to tell the two apart.

**Front-end bugs, now fixed:**

- A member name must keep its **written** spelling and must not be resolved
  through the page-wide declaration map. A member's meaning depends on its
  receiver's type, which the parser does not know. In the showcase a class
  field `dropped` and a choice variant `Dropped` fold to one key, so the
  variant silently acquired the field's spelling, emitted a plain `CALL`
  instead of `NEW_VARIANT`, and was rejected much later by the fault contract.
  The cost of the fix is that member access is case-sensitive while everything
  else folds; closing that gap needs the type checker, which knows the
  receiver type.
- `raise Payload with f being v` must construct the payload directly. Parsing
  it as an ordinary expression made the `with` clause a call, and the checker
  then saw a payload of type `any`.
- Annotation arguments must not cross a line boundary, or the following
  declaration's name is read as another argument and the declaration appears
  to start at its `receives` clause.
- A variadic parameter collects into `array<T>`, which Ny's parser does for
  `..name: T` and Matrix was not doing.

**Runtime limitations, confirmed against hand-written Ny:**

- **An array of class handles is not executable on the interpreted route.**
  `let items: array<Shape> = [a, b]` compiles to `NEW_ARRAY_LITERAL 2 Shape`
  and the VM rejects it: "class value cannot be used by this untyped, scalar,
  or raw-pointer operation". `classes.md` says aliases "remain aliases through
  collection", which overstates what the interpreter executes.
- **An indirect call cannot appear in a function that has fault regions.**
  A paragraph value called inside a paragraph that also handles faults fails
  with "call in a fault-bearing function lacks a supported exact result
  signature". This is an undocumented interaction between two features, and
  the showcase now keeps its paragraph-value demonstration in a separate,
  fault-free paragraph.
- **A parallel worker may not read a book constant.** A book-level constant is
  a global, so reading one emits `LOAD_GLOBAL`, which
  `concurrency_contract.cpp` forbids across a worker's transitive call graph.
  The showcase originally did read two mask constants and the verifier caught
  it on the NIL artifact — exactly the default-deny effect check that the
  authority design is meant to generalise.

One fixture opts out of byte-for-byte output comparison through an
`.exit-only` marker, because the showcase reads the clock. It is still required
to compile and exit 0.

## Stage 7: cross-language linking

`matrix/samples/14-cross-language*.matrix` plus `14-cross-language-helper.ny`
compile into **one program across two surfaces**, in topological order:

```text
tour::mixed       (Matrix)  declares Measurement and Sampler
      v
tour::ny_helper   (Ny)      derives OffsetSampler from a Matrix class,
                            constructs a Matrix record
      v
tour::app         (Matrix)  calls an Ny paragraph, uses the Ny class through
                            a Matrix-typed ancestor view
```

The load-bearing line is the ancestor view: an **Ny class deriving from a
Matrix class, with Ny's override selected through a Matrix-declared base**.
That only works because NIL's class section records ancestry independently of
which surface produced it.

Three pages rather than two, because the obvious arrangement — a Matrix book
and an Ny module that each use the other — is a module dependency cycle that
Ny cannot order. The sample originally had exactly that cycle and reported
"Module dependency cycle detected among input files".

One front-end fix was needed: a **qualified** name is a path, so its dots
normalise to `::` wherever it appears — type position, construction, `inherits`
and expression position alike. `resolve_name` now does that centrally, and
never looks a qualified name up: it names a declaration on another page whose
spelling this page cannot see, which is precisely why a Matrix client must
spell an Ny name the way Ny declared it.

A second fix came out of sample 07: a **member declaration** keeps its written
spelling, exactly as member access does. Folding it against the page-wide map
aliases unrelated names — a contract `Fingerprint` and its method
`fingerprint` are one key, so the method silently took the contract's
spelling. Only a free paragraph is a top-level name. With that fixed, sample
07 runs against the real `ny.lang.core`, with generic bounds and associated
functions resolving.

### Extensions on primitives: an Ny codegen bug, now fixed

Extensions on user-declared types always executed. Extensions on tier-one
primitives did not, which mattered because the two-tier type scheme leans on
exactly that case — the language keeps `text` knowing nothing about encoding,
and the library grows its surface.

```text
text begin extension      ->  CALL: unknown function 'String__shouted'
number begin extension    ->  CALL: unknown function 'i64__doubled'
```

The cause was in `codegen.cpp`, not in either front end. A method's NIL symbol
is built from the impl block's target type, while the **call site** builds it
from the receiver's *inferred* type. For a user-declared type both are the
module-qualified declaration name, so they agree. For a builtin the inferred
type is the bare canonical name (`String`, `i64`), so module-qualifying the
declaration made the two disagree:

```text
function demo__prim__i64__doubled   <- declared
CALL     i64__doubled               <- called
```

Hand-written Ny failed identically for `impl i64`, `impl text` and
`impl String`, which is how it was identified as an Ny bug rather than a
Matrix one. Builtins are now never module-qualified, at both the registration
and emission sites, and `self` takes the canonical builtin type so the
receiver matches. `text`, `string` and `String` all resolve to one symbol, as
do `int` and `i32`.

The regression test is on the Ny side, where the bug was:
`ny.routes.primitive-extension` covers `i64`, `u8`, `bool` and `text` across
the four interpreted routes.

One consequence is worth recording: an extension on a builtin now lives in a
**global namespace**, because the call site mangles a bare type name with no
module to consult. Two books cannot both add `shouted` to `text`. This follows
from the existing dispatch design rather than from the fix, and container
builtins (`array`, `ptr`, `ref`, `vec`, `slice`) were deliberately left alone:
they have their own dispatch paths and no fixture motivates changing them.

Everything else is diagnosed as *not implemented in this stage* with a pointer
to this document. That is deliberate: a page that parses cleanly must not
overstate what was lowered, so unimplemented constructs fail loudly rather
than being skipped.

Four bugs were found by running the corpus rather than by reading it, which is
the argument for the execution fixtures existing at all:

- `a` was a reserved keyword, inherited from `on a reference`. It is the single
  most common variable name and, unlike most collisions, the hyphen rule cannot
  rescue it (`a-b` is a different name). The receiver clauses dropped the
  article: `on reference`, `on mutable receiver`, `without receiver`.
- A path segment may be a keyword. `ny.lang.text`, `ny.lang.io.file` and
  `demo.text` all contain one, and refusing them made much of the standard
  library unnameable. Safe to allow, because the lexer only emits a `dot` token
  when a name character follows.
- `codegen.cpp` tests `variables.size() == 1` on a for-in loop, so the
  single-variable `ast_for_in_stmt` constructor — which leaves that vector
  empty — silently produced an unresolvable loop variable.
- `as` and `then` were missing from the keyword table entirely, caught by the
  grammar-sync check rather than by a failing parse.

Stage 6 adds `attempt`/`caught`/`always` with `provided` guards and the opaque
catch-all token, `raise ... with` and `raise ... again`, owned buffers with
lexical borrowing and moves, raw pointers, references, `apply ... across`,
the clock intrinsics, `context`, arbitrary `note` annotations, intrinsic
declarations, `test` blocks and inline assembly. Every one of them lowers to
a construct that already existed: the stage contributed no AST node and no
checker rule, which is the claim the whole design rests on, and it is now
tested rather than asserted.

Its exit condition was `samples/08`–`samples/10` and `samples/13` on their
supported routes, interpreted routes that reject inline assembly rather than
emulate it, and ownership violations that are parse errors at the Matrix
construct. All four samples now run on **all six execution routes** — the
samples themselves are the route fixtures, not copies, because the README
calls the corpus normative and a fixture that has drifted from its sample
would prove nothing about the sample.

Two things needed evidence the samples could not give:

- **Sample 13 declares inline assembly and never calls it**, so it cannot show
  what an interpreted route does with an assembly program that *runs*.
  `tests/matrix/routes/inline-assembly-called.matrix` calls it. The four
  interpreted routes refuse with
  `INLINE_ASM requires a matching LLVM native route`, and the LLVM routes
  execute the same NIL and print `bswapq(1)`. Both halves are registered
  tests; neither is an emulation.
- **Fault payloads have nominal identity**, which makes faults the one stage-6
  feature with a cross-language claim attached: an Ny `raise` should be
  catchable by a Matrix handler over the same record. `faults-twin.ny` is the
  same program hand-written in Ny, and `matrix.execution` compares the two
  artifacts' struct sections byte for byte. They match, so
  `tour::faults::ReadFault` is one type in both languages, not two types that
  happen to print alike.

One surface rule needed a diagnostic to be usable, and it was written the
wrong way round at first. Ownership operations are **statements**, because Ny
accepts an ownership operation only as a direct local declaration
initialiser, so there is no expression form to lower to. The parser rejected
`value copy is own ...` with the generic `expected a value, found 'own'` —
correct, but it named the token rather than the rule, and the author of that
mistake cannot tell whether `own` is banned in values or merely misused here.
It now reads `'own' is a statement, not a value`, and
`tests/matrix/diagnostics/ownership-as-expression.matrix` keeps it that way.
The other ownership rules — owners and views are local-only, never
parameters, fields, globals or captures — are **not** parse rules: they are
Ny's affine checker's, and they fire unchanged on the shared AST with Ny's
own wording, which is exactly the inheritance the design predicts.

The `own`/`borrow`/`move` statement-position check is deliberately placed in
`parse_primary` and covers the whole family (`allocate`, `release`,
`assembly`, `attempt`, `raise`, `set`, `say`) rather than only the ownership
three, because the rule is one rule: *these constructs are statements*. A
future construct added to the family inherits the rejection by being listed
there, not by remembering to add a case.

**1. Freeze the surface.** Resolve the five open decisions in
[`../README.md`](../README.md). Settle the keyword table and the reserved-word
list. Exit: this folder's samples are the normative corpus and the grammar
sketch is an LL(2) grammar with no remaining marked ambiguities beyond the two
documented ones.

**2. Lexer, folding and reserved words.** Keyword table, ASCII case folding,
hyphenated identifiers, the folded lookup key, the backtick escape. Decide
reserved-vs-contextual keywords here; it changes the lexer's architecture.
Exit: every file in `samples/` tokenises; a negative corpus covering
`total-1`, `-kind`, `kind-`, `frame--kind`, a keyword collision that
capitalising does not escape, and a non-ASCII identifier that must not fold,
each produces an exact diagnostic naming the rule. The formatter that
normalises keywords to lowercase and references to their declaration's
spelling ships with this stage, not after it — see the README on why folding
without a formatter trades errors for diff noise.

**3. Parser for the scalar subset.** Areas, links, functions, bindings,
word operators, `when`, `repeat`, `say`, `give`. Exit:
`samples/01`–`samples/04` compile to NIL and run on all six execution routes
with exact stdout and exit status, matching hand-written Ny equivalents
byte-for-byte in output.

**4. Aggregates.** Records, choices, generics, aliases, arrays, pointers.
Exit: `samples/05` on all six routes, plus NIL schema equality against the Ny
equivalent — the same section 1 records, not merely a program that prints the
same numbers. This is the first real cross-language proof.

**5. Classes and contracts.** Including the `my` receiver synthesis. Exit:
`samples/06`–`samples/07` on all six routes, and section 7 class records
identical to the Ny equivalent's.

**6. Faults, ownership, parallelism, assembly.** Exit:
`samples/08`–`samples/10` and `samples/13` on their supported routes, with the
interpreted routes correctly *rejecting* inline assembly rather than emulating
it, and ownership violations producing parse errors at the Matrix construct.

**7. Cross-language linking.** Exit: `samples/14-cross-language.matrix` plus
`samples/14-cross-language-helper.ny` compile together into one program that
runs on all six routes, Matrix deriving from Ny and Ny deriving from Matrix,
followed by the stage-1 binary-library path.

**8. Tooling.** `nillsp` gains a Matrix mode, `vscode` gains a `.matrix`
grammar, and `--debug-info` v5 source maps carry `.matrix` paths so
`nildebugger --dap` steps Matrix source. Exit: F5 source stepping in a `.matrix`
file, with named locals.

## What this plan does not claim

It does not claim Matrix is easier to compile than Ny, that a natural-language
surface reduces semantic errors (it targets *syntactic* errors, which is a
narrower and more defensible claim), or that agents will write better programs
in it. Those are hypotheses and the corpus in `samples/` is the thing to test
them against. It also does not claim the two languages will stay in sync
automatically: they stay in sync because they share a checker, and the moment
someone adds a Matrix-only AST node that invariant is gone.
