# NIL Framework Notes

## Project overview

### Purpose
The NIL Framework is an open-source programming language framework for systems
and application development. Version 1.0.0-rc.1. It is built around **NIL**, a
stack-based intermediate representation, with two surface languages:

1. **Ny** — systems syntax with familiar function declarations, explicit types,
   symbolic operators. Combines records, classes, and generic programming.
2. **Matrix** — natural-language word-order syntax designed for AI agents and
   human readability. Uses declarations that read as sentences, named arguments,
   and operators written as words.

Both languages share one type system, one standard library, one runtime, one
interpreter (`nilinterpreter`), and one native backend (LLVM). They compile
together in the same program.

### Architecture
```
.Matrix source  ─┐
.Matrix/.mat ────┤   Matrix frontend (matrix_lexer, matrix_parser)
                 ├─> Shared AST ─> Type checker ─> NIL (.nil) ─> VM / native
.Ny source ──────┤   Ny frontend (preprocessor, lexer, parser)
                 └─> Shared AST
```

### Key directories
- `matrix/` — Matrix language: guide, README, grammar, samples (18 files), docs.
- `nilcompiler/` — The compiler (C++). Includes both Ny and Matrix frontends,
  AST, type checker, codegen, LLVM backend, and the executable context.
- `nilinterpreter/` — VM interpreter with AI control server.
- `nilstd/` — Shared standard library (Ny/Matrix): 68 Ny + 12 Matrix sources in
  18 logical SDK modules.
- `nilassembler/`, `nillinker/` — Separate assembly and linking tools.
- `nildebugger/` — Debugger.
- `nillsp/` — Language server protocol.
- `nilbuild/` — Project build tooling shared with VS Code.
- `nilaix/` — Experimental UI/AI sandbox (NOT stable; not installed as a library).
- `examples/`, `tests/`, `docs/`, `notebook/`.

## Matrix language

Matrix IS A NIL module — a `book <name>` declaration is a namespace, and the
file is found by book path under an include root. It compiles through the
shared AST, type checker, and NIL generator, so Matrix and Ny interoperate
within a single program — they emit the same NIL.

### Core design principles
1. Words, not punctuation (`begin/end paragraph` instead of `{}`).
2. One canonical phrasing per construct.
3. Word reuse only when the relation is the same (`when` = "in the case of"
   in conditionals, match arms, and choice variant declarations).
4. A name is one token; hyphen joins words, case folds ASCII-only.
5. Every block closes with its own name (`begin paragraph`…`end paragraph`).
6. The declaration reads as a sentence; every declaration is
   `<modifiers> <name> <clauses...> begin <kind> … end <kind>`.
7. Named arguments, lowered to positional after checking.
8. The surface must not out-promise NIL: no closures with captures, no dynamic
   trait objects, no generic classes, no postfix `?`, no overload sets, no
   field defaults, no async/channels — only what the runtime can execute.

### Universal declaration shape
`<modifiers> <name> <clauses...> begin <kind> … end <kind>`
Kinds: paragraph (function), record, choice, class, contract, implementation,
extension, test. Learn one pattern.

### Types (two-tier)
- Tier 1 (ergonomic): text, number, floating, boolean, nothing
- Tier 2 (machine rep, via `override`): override integer (i32), override long
  (i64), override byte (u8, wrapping), override double (f64)
- Machine types are NOT legal bare; `as override byte` not `as byte`.
- Casting is `becomes`. Compound: `array of T`, `pointer to override byte`,
  `reference to T`, `owned array of T`, `shared view of byte`,
  `Envelope of number`, `Result of T, fault-record`.
- `short`, `unsigned`, `decimal`, `single`, `letter` are reserved and rejected.

### Memory model
- Owned buffers (`own variable x as array of override byte sized N`), shared/
  exclusive borrows (`borrow reader from x`, `borrow writer from x exclusively`),
  moves (`move x into y`). Lexical lifetimes.
- Raw pointers (`allocate p as pointer to override byte sized N`, `release p`).
- Ownership operations are STATEMENTS, not expressions. Owners/views cannot be
  parameters, results, fields, or globals.

### Fault model
- Payloads are fully-initialized concrete records. No scalar/class/choice ptrs.
- Handler matches exact record identity (book + generics).
- `always` = finally cleanup; `give` inside it replaces the suspended exit.
- Catch-all `caught anything as token` binds opaque token, may only re-raise.
- Division by zero, invalid NIL, panics — fatal, not catchable.

### Parallelism
`apply W across I` — bounded fork/join. Worker must be a declared paragraph
with one scalar param, one scalar result. Worker may NOT touch: globals,
pointers, owned buffers/views, record handles, arrays, text, class methods,
nested apply, assembly, foreign calls, console/file I/O, process ops, clocks,
sleeps, fault payloads. Verified on NIL (not just source). Max 1,048,576 inputs.

### What will bite you
- Reserved words: value, text, number, set, read, move, type, at, in, to, of,
  the, my, short, single, etc. Fix = hyphenate (read-file, value-held, type-name).
  Backticks escape a bare collision as last resort.
- A value CANNOT be named after its type (case folds: `tagged` == `Tagged`).
- Member access is case-sensitive (`obj.Dropped` ≠ `obj.dropped`).
- `main` must say `returns number`; lowers to 32-bit (runtime entry contract).
- Array literals need element type declared (`[1, 2, 3]` as `array of number`).
  Integer literals infer 32-bit; `number` is 64-bit — widths would disagree.
- Indirect calls CANNOT appear in paragraphs that handle faults.
- Array of class handles does NOT execute on the interpreted route.
- Parallel worker may not read book constants (they're globals).
- Two books cannot add the same method to one type.
- `say` takes exactly one value. Use separate statements.
- Zero-argument calls need `with nothing`; chaining needs parens.

### Route honesty (what's interpreter-only)
- Scalar-to-text conversion (interpreted only).
- Paragraph values and anonymous paragraphs (interpreted only; native indirect
  calls not implemented).
- Inline assembly (x86_64 native only; interpreted routes reject it).

### Not available (deliberately unspelled, runtime can't execute them)
field defaults, overload sets, capturing closures, dynamic contract objects,
generic class execution, downcasts, automatic destructors, postfix `?`,
default contract method bodies, supertraits, associated types, i16/u16/u32/u64/
f32 arithmetic, async, channels, task handles.

## Standard library (nilstd)

18 logical SDK modules: prelude + core, collections, datetime, db, diagnostics,
io, math, net, nildata, reflection, runtime, security, system, testing, text,
threading, ui.

### nilstd/src/core
- `traits.ny` — Clone, Display, Debug, Default, Eq, Hash (as `trait`).
  Note: Ny uses `trait`/`impl` syntax here (not `contract`).
- `option.ny` — Option<T> with Some/None, is_some, is_none, is_some_and,
  unwrap, map, and/or, etc.
- `result.ny` — Result<T,E> with Ok/Error variants.
- `error.ny` — Standard error type with chaining.
- `ordering.ny` — Ordering enum + comparison helpers.
- `sorting.ny` — Array sorting, searching, aggregation, shuffle.
- `sorting_indices.matrix` — Checked sorting in Matrix.
- `convert.ny` — Checked text/number/base/byte/scalar conversions.
- `algorithm.ny` — Bounded integer algorithms.
- `faults.ny` — Fault and panic infrastructure.
- `assert.ny` — Assertion helpers.
- `dynamic.matrix` — Dynamic typing support.
- `package.ny` — Package declaration for nil.std.core (imports all above).

### nilstd/src/collections
- Arrays (fixed), Vec (resizable), List, Dictionary, HashMap, HashSet, Queue,
  Stack, Set, Map, Range, Iterators + interfaces (ICollection, IDictionary,
  IList, ISet).
- `query.matrix` — Checked query terminals written in Matrix: query-count,
  query-any, query-first, query-last, query-single.
- `array_checked.matrix` — Checked array operations (try-get, try-gather, try-set,
  try-fill-range, try-copy-range). Validates before writing, handles shared-array
  aliases, copy direction.
- `hash_map.ny` — Deterministic hash map with parallel vectors, collision-safe
  equality, length_cell array for length tracking.

### nilstd/src/io
- console.ny, file.ny, stream.ny, path.ny, binary.ny, varint.ny, checksum.ny
  (note: file_system is experimental).

### Cross-language compatibility
- Matrix book IS a NIL module; Ny imports it as ordinary module import.
- Both emit the same NIL with same module::name storage identities.
- Book paths normalize '.' to '::'. A declaration's spelling with '-' rewritten
  to '_' is the emitted NIL symbol (Ny identifiers admit no hyphen).
- Matrix-internal references fold case; cross-language references do NOT
  (Ny is case-sensitive; spell Ny names exactly as declared).

### Key findings / gotchas discovered
1. Comments: line-only `!!!`; no block form (mutually ambiguous with blank
   `!!!` lines).
2. `len` and `push` are compiler builtins, not library functions — take
   positional args, no parameter names. So Matrix uses `the length of values`
   and `append x to values`.
3. Reserved words collide with good field names (`value`, `read`, etc.) — hyphenate.
4. Hash map uses `length_cell: array<i32>` (single-element array) as a field,
   indexed to track length — unusual pattern worth noting.
5. `nil.std.core` package uses Ny `module`/`import`/`trait`/`enum`/`impl` syntax
   while Matrix files in the same package use Matrix syntax (book/read/paragraph).
