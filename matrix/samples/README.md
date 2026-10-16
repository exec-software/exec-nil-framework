# Matrix samples

Proposed syntax, in reading order. None of this compiles yet: the Matrix front
end does not exist. These files are the normative corpus the front end will be
built against, and the place to argue about spelling before anything is
implemented.

Every construct here maps to something Ny already executes. Where a sample
comment says a route rejects an operation, that is Ny's existing behaviour
being inherited, not a Matrix limitation.

| File | Covers | Ny reference |
| --- | --- | --- |
| [`01-hello.matrix`](01-hello.matrix) | `area`, `main`, `say`, both comment forms | README "First program" |
| [`02-bindings-and-types.matrix`](02-bindings-and-types.matrix) | scalars, bindings, casts, word operators, arrays, raw pointers | `docs/language-reference/types.md` |
| [`03-control-flow.matrix`](03-control-flow.matrix) | `when`, every `repeat` form, `consider`, both as expressions | `docs/language-reference/statements.md` |
| [`04-paragraphs.matrix`](04-paragraphs.matrix) | parameters, named arguments, mutable parameters, variadics, recursion, function values | `docs/language-reference/functions.md` |
| [`05-records-and-choices.matrix`](05-records-and-choices.matrix) | records, choices, payloads, generics, aliases | `docs/language-reference/types.md` |
| [`06-classes.matrix`](06-classes.matrix) | classes, inheritance, `my`, virtual/abstract/overriding, ancestor views | `docs/language-reference/classes.md` |
| [`07-contracts.matrix`](07-contracts.matrix) | contracts, implementations, bounds, associated functions | `docs/language-reference/traits.md` |
| [`08-faults.matrix`](08-faults.matrix) | `attempt`, `caught`, `always`, `raise`, catch-all tokens | `docs/language-reference/faults.md` |
| [`09-memory.matrix`](09-memory.matrix) | owned buffers, borrows, moves, raw pointers, references | `docs/advanced/owned-buffers.md` |
| [`10-concurrency-and-time.matrix`](10-concurrency-and-time.matrix) | `apply ... across`, clock operations | `docs/advanced/structured-concurrency.md` |
| [`11-books.matrix`](11-books.matrix) + [`11-books-pricing.matrix`](11-books-pricing.matrix) | areas, link forms, visibility, area globals, `::` | `docs/language-reference/modules.md` |
| [`12-library.matrix`](12-library.matrix) | bundles | `docs/language-reference/modules.md` |
| [`13-context-and-interop.matrix`](13-context-and-interop.matrix) | `context`, annotations, tests, intrinsics, inline assembly | `docs/advanced/executable-context.md`, `docs/advanced/inline-assembly.md` |
| [`14-cross-language.matrix`](14-cross-language.matrix) + [`14-cross-language-helper.ny`](14-cross-language-helper.ny) | Matrix and Ny in one program, both directions | `docs/advanced/nil-format.md` |
| [`15-telemetry-relay.matrix`](15-telemetry-relay.matrix) | **everything at once** — a complete single-file application | all of the above |
| [`16-names-and-case.matrix`](16-names-and-case.matrix) | case folding, hyphenated names, cross-language identity | `docs/language-reference/lexical-structure.md` |

## A note on spelling across the corpus

`15-telemetry-relay.matrix` and `16-names-and-case.matrix` use Matrix's recommended
style: lowercase, hyphen-joined names (`frame-kind`, `pack-header`,
`MAX-PAYLOAD`). Samples 01–14 retain `CamelCase` and `snake_case`, which is
not an oversight — every one of those spellings folds to the same name, and
leaving them mixed is the demonstration. A formatter normalises a real
codebase to the declaration's spelling; see
[`../README.md`](../README.md#case-folding-and-hyphens) for why that formatter
is not optional.

## Start with 15 if you want to judge the surface

[`15-telemetry-relay.matrix`](15-telemetry-relay.matrix) is a telemetry relay that
packs and unpacks a 32-bit header with bitwise manipulation, stages payloads
in an owned byte buffer, folds a CRC-32, serialises through a raw pointer,
converts byte order with inline assembly, encodes samples in parallel, and
dispatches through a class hierarchy — raising typed faults throughout. It
uses every construct in the table above in one file, which is the only way to
find out whether they read well together.

Three things were discovered by writing it, and all three are now fixed in the
proposal rather than left for the parser to hit:

- **The draft's two comment forms are mutually ambiguous.** A bare `!!!` used
  as a blank line inside a comment run is indistinguishable from a block
  delimiter. The header of that one file contains 25 such lines. Matrix now
  has line comments only.
- **`len` and `push` are compiler builtins, not library functions**, and take
  positional arguments with no declared parameter names — so the
  named-argument form has nothing to bind against. They became `the length of
  values` and `append x to values`.
- **Reserved words collide with the best field names.** `field value as long`,
  `receives value as text` and `my value` all read perfectly and all collide
  with the `value` binding keyword. See
  [`../README.md`](../README.md#reserved-words-are-the-real-cost-of-a-word-based-surface).

## The shape to notice

Every declaration is the same sentence:

```text
<modifiers> <name> <clauses...> begin <kind>
    ...
end <kind>
```

That holds for `function`, `record`, `choice`, `class`, `contract`,
`implementation` and `test`. One pattern to learn, one pattern to generate,
and a closing token that names what it closes.

## Open spellings

The samples commit to an answer so they can be read end to end. These are the
answers most worth disagreeing with, and the reasoning is in
[`../README.md`](../README.md#open-decisions):

- `f with nothing` for a zero-argument call — regular, but the most verbose
  consequence of making calls syntactically unmistakable.
- Word operators for arithmetic — unambiguous, but `(count plus limit) over 2`
  is a real cost in numeric code.
- `always` rather than Ny's `recover` — clearer about finally semantics, at the
  price of the two languages reading differently about one NIL construct.
