# Diagnostics and analyzers

> Current implementation: [compiler analysis and reflection](../../docs/analysis-api.md)
> provides live compiler checks, safe formatting fixes, and generated declaration
> snapshots. The richer analyzer hosting and semantic graph below remain design work.
> The [project driver and default rules](../../docs/tools/projects.md) now share
> project context and analyzer policy with VS Code, including conservative
> unreachable-code and discarded-checked-result warnings.
> Workspace source requests include unsaved dependencies in compiler checks.
> An initial compiler binding index now supplies exact source definitions;
> complete semantic queries and analyzer hosting remain follow-up work.


Two requirements that turn out to be one design.

**The ask.** Agents work well in Ny and badly with Ny's errors. Matrix is the
chance to fix that properly, especially at compile time. Separately, tooling
needs Roslyn-style extensibility: third parties writing analyzers against the
compiler.

**Why they are one design.** An analyzer's entire output is diagnostics. If the
diagnostic model is designed only for the compiler's own messages, every
analyzer is then limited to what that model can say — no span, no code, no
structured data, no fix. Design the diagnostic once, with analyzers as the
intended producer, and the analyzer API becomes "things that emit these".

The sections below preserve the original design proposal. Compiler-backed
diagnostics and declaration snapshots now exist; see the current implementation
link above. Rich semantic queries and custom analyzer hosting remain future work.

## What is wrong today

```text
Type checking failed:
  line 5: Binary operator type mismatch: 'i64' vs 'String'
```

Six distinct failures in one line:

1. **No file.** Harmless with one page, useless with twenty.
2. **No column**, so no caret and no span.
3. **No source excerpt.** The reader has to go find line 5 themselves.
4. **The wrong vocabulary.** The author wrote `number` and `text`. `i64` and
   `String` are Ny's internal spellings and do not exist in Matrix. This is
   the worst of the six: the message is in a language the author is not
   writing, exactly where they are already confused.
5. **No remedy.** It says what is wrong, never what to do.
6. **No code.** Nothing to suppress, document, look up, or group by.

Runtime contract errors are worse, because they report instruction indices
rather than source positions, even though NIL carries a `debug_line` per
instruction:

```text
NIL fault contract in function 'main' at instruction 712: call in a
fault-bearing function lacks a supported exact result signature
```

Finding instruction 712 means disassembling the artifact. I did it three times
in this session, and the information to avoid that was already in the file.

## The diagnostic model

One record, emitted by the compiler and by analyzers alike.

| Field | Purpose |
| --- | --- |
| `code` | Stable identifier, e.g. `MX0412`. Suppressible, documentable, greppable. |
| `severity` | `error`, `warning`, `note`, `help`. Analyzers mostly emit the middle two. |
| `message` | One sentence, in **the surface's own vocabulary**. |
| `primary` | File, line, column, length — the span the caret sits under. |
| `secondary` | Further labelled spans: the other operand, the original declaration, the conflicting import. |
| `hint` | What to do, concretely. |
| `fix` | Optional machine-applicable edit: span plus replacement text. |
| `data` | Optional structured payload for tooling. |

Rendered:

```text
error[MX0412]: cannot add a number and a text value
 --> dispatch.matrix:5:11
  |
5 |     say total plus "text"
  |         ----- ^^^^ ---- this is text
  |         |
  |         this is a number
  |
help: convert one side explicitly, for example
      `total becomes text plus "text"`
```

Four properties matter more than the formatting:

- **Surface vocabulary.** `number`, not `i64`. `text`, not `String`. The
  renderer maps NIL spellings back through the same table the parser used to
  lower them, so a Matrix author never sees an Ny type name. Where the author
  wrote `override long`, that is what the message says.
- **Spans, not lines.** The Matrix lexer already records a column on every
  token, so parser diagnostics can carry exact spans today. The shared type
  checker carries only a line; giving `ny::diagnostic` a span is a separate,
  self-contained change that improves both languages.
- **A machine-applicable fix** where one is unambiguous. This is what makes an
  analyzer worth writing and an agent's edit loop cheap.
- **A stable code.** Without one there is nothing to suppress and nothing to
  document, and every tool reinvents message-substring matching — which is
  exactly what `tests/matrix/run_execution_tests.py` does today, and it is a
  compromise, not a model.

## Output forms

Two renderers over one record:

- **Human**: the block above. Colour when the stream is a terminal.
- **JSON Lines**: one object per diagnostic, `--diagnostics json`. This is what
  an editor, a CI annotator, or an agent consumes, and it is the same shape an
  analyzer produces. The repository already has the precedent:
  `nildebugger --json` is newline-delimited JSON for automated sessions.

## Analyzers

An analyzer is a function from a compilation to diagnostics. Three decisions
matter, and all three are security decisions as much as design ones — which is
the point at which this stops being a convenience feature and starts being part
of the language's security story.

**1. What an analyzer sees.** A read-only view: the AST, the resolved symbol
table, the type assignments, and the NIL module. Read-only is not politeness —
an analyzer that can mutate the compilation is a compiler, and then its output
is not a diagnostic but a miscompile.

**2. When it runs.** Three phases, because useful analyzers want different
things: after parse (syntactic and style rules), after type checking (the
largest and most useful class), and after NIL generation (the effect and
authority rules, which must read the artifact — the same reason the kernel
reads the artifact rather than the source).

**3. How it is loaded, and this is the hard part.** Roslyn loads analyzer
assemblies into the compiler process, so an analyzer is arbitrary code running
with the compiler's authority. For a project whose premise is that agents and
bad actors must not break out, shipping that model unexamined would be
self-defeating: building a language to sandbox untrusted code and then
executing untrusted plugins in the compiler is the same mistake one layer up.

Three options, in increasing order of what they cost to build:

- **Declarative rules.** A pattern language over the AST, no arbitrary code.
  Safe, inspectable, and genuinely sufficient for a large fraction of real
  analyzers — naming conventions, banned constructs, required annotations.
- **Analyzers written in Matrix, run on the VM.** The compiler hosts the
  interpreter, an analyzer is a Matrix book declaring its authorities, and the
  existing capability machinery bounds it. Self-hosting, and the sandbox is the
  one the project is already building rather than a second ad-hoc one.
- **Native plugins.** Fastest and most expressive, and an arbitrary-code
  execution surface in the toolchain. If it exists it needs signing and an
  explicit opt-in, and it should not be the default path.

The recommendation is to start declarative, because it covers the common cases
and cannot be weaponised, and to treat the Matrix-on-VM option as the real
target — it is the one that makes the language's own sandbox the tooling
sandbox, which is a considerably stronger position than Roslyn's.

## Sequence

1. The diagnostic record, the human renderer, and surface-vocabulary mapping,
   in the Matrix front end where spans already exist.
2. JSON Lines output over the same record.
3. Spans on `ny::diagnostic`, so the shared type checker can point at a span
   rather than a line. Improves Ny too.
4. Source positions on NIL and VM contract errors, from the `debug_line` the
   artifact already carries.
5. Analyzer phases and the read-only view, declarative rules first.

Steps 1 and 2 are self-contained and do not touch the Ny front end. Step 3 is
the one that needs care, because every existing diagnostic site has to keep
compiling.
