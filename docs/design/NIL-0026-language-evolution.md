# NIL-0026: evolving the language without breaking what is written

- **Status:** §4 implemented 2026-11-01 (`edition` in the manifest, validated,
  described, and threaded to the interpreter). §6 items 4 and 5 remain.
- **Priority:** Process and core-tooling direction
- **Components:** `nilbuild`, `nilcompiler`, `nilinterpreter`, project manifest

## Why now

NIL is young enough that we are the only people writing it, and that is
exactly the moment to decide this — not because a stranger's code will break,
but because *our own corpus already matters*. The Pensieve curriculum pins
Matrix constructs by hash; the validation projects are compiled as evidence.
A silent change in meaning does not just break a program, it invalidates a
measurement.

Three changes landed on 2026-11-01, and they had three different compatibility
profiles. That is the whole design problem in miniature.

| change | profile | what it needed |
| --- | --- | --- |
| Unix socket for the NIL-0006 agent server | pure addition — no existing behaviour had an opinion | nothing; just ship it |
| strict array bounds | **changes what a valid program means** | opt-in, default untouched |
| automatic `.nycrash` artifacts | new side effect, and a new stderr line | split: keep the side effect, gate the output |

The middle one is the interesting case. Out-of-bounds access is silent in NIL —
a read past the end gives the element default, a write is dropped — and that is
not an oversight. `tests/language/array-bounds-consistency.ny` pins it
deliberately. Making it an error broke 9 of 539 tests, and would have changed
the meaning of every NIL program ever written.

The third is the subtle one. Writing a crash file broke nothing. Printing one
extra line to stderr broke six route tests, because a crashing program's
stderr is something tools parse. **The observable surface is wider than the
language.**

## 1. Three kinds of change, and the rule for each

**Additive.** New capability, no existing program observes a difference. Ship
it on by default. The Unix socket is this: before, the feature did not exist on
the platform; now it does.

**Diagnostic.** Changes what you are *told*, never what the program *means*.
Must be reversible per run and must never alter output a program produced
itself. `NIL_STRICT_BOUNDS=1` is this — the program computes exactly the same
values; the runtime merely refuses to stay quiet about one of them.

**Semantic.** Changes what a valid program means, or what counts as valid.
Never silent, never default, never a global flag. This is what §4 is for.

The test for which bucket you are in is one question: *could an existing,
correct program produce a different result, or a different observable output,
without being edited?* If yes, it is not additive, whatever it looks like.

## 2. Environment variables are for diagnostics only

`NIL_STRICT_BOUNDS` and `NIL_CRASH_REPORT` are deliberately environment
variables, and deliberately not language features. An env var is:

- **ephemeral** — it describes this run, not this program;
- **not reviewable** — it leaves no trace in the source or in review;
- **not reproducible** — a colleague running the same code gets different
  behaviour, and nothing in the repository says why.

Those are fatal properties for semantics and harmless ones for diagnostics. So
the rule is absolute: **an environment variable may change what NIL tells you,
and may never change what NIL computes.** A flag that makes a program return a
different answer belongs in the manifest, where it travels with the code.

## 3. The observable surface, stated explicitly

Backward compatibility is usually discussed as "does the program still compute
the same thing". That is too narrow. The surface we must not break by accident:

1. values computed, and control flow taken;
2. **stdout** — a program's own output;
3. **stderr** — including the exact text of diagnostics, which tools parse;
4. exit codes;
5. the shape of artifacts a program writes;
6. the serialized forms: `.nil`, `nildata`, `nycrash/1`.

Items 3 and 6 are the ones that get forgotten. Today's crash reporter reuses
the existing `nycrash/1` format rather than inventing a better one, and
distinguishes itself with a new `source = interpreter_self` value instead of a
new format version — so the Enigma `parse_nycrash` reader keeps working
without being taught anything.

## 4. Proposed: editions in the project manifest

For semantic change, NIL should adopt editions, and the place for them already
exists. `nil.project.nildata` is already versioned and already carries
per-project behaviour:

```
nildata 1
object
  schema is "nil.project.v1"
  name is "inventory-system"
  entry is "src/main.matrix"
  analysis is an object
    disabledRules is an array
    end array
    warningsAsErrors is false
  end object
end object
```

The proposal is one field:

```
  edition is "2026"
```

With these rules:

- **A manifest with no `edition` means the original edition**, forever. Every
  project written before this exists keeps its current meaning with no edit.
- An edition fixes semantics for that project. `strictBounds` becomes an
  edition property rather than an env var: projects opting into a later
  edition get bounds checking as *language behaviour*, reproducibly, visibly,
  and in version control.
- The compiler accepts every edition it has ever supported. Editions are not
  deprecation; nothing is removed.
- One edition per project, not per file. A project whose files disagree about
  what `xs at 99` means is worse than either answer.
- `nilbuild` reports the edition in `describe`, and records it in build
  artifacts, so a measurement can state which semantics produced it.

This is Rust's model rather than Python's. Nathan's instinct was Python's
`from __future__ import`, which is the right idea at the wrong scope: a
per-file pragma means one project can hold two meanings of the same syntax at
once, and the NIL manifest already gives us a project-wide place that a `.py`
file never had.

## 5. What this would change about today's work

`NIL_STRICT_BOUNDS` stays as a diagnostic — it is genuinely useful to flip for
one run while hunting a fault, without editing anything. But the *language*
answer to "arrays should be bounds-checked" is an edition in which they are,
and at that point the env var becomes a way to get the new edition's checking
in an old-edition project, which is exactly what a debugging switch should be.

## 6. Status

Implemented 2026-11-01:

1. ✅ `edition` in the project schema, accepted and echoed by `describe`,
   defaulting to `original` when absent. An **unknown edition is refused**
   rather than ignored — a project asking for semantics this toolchain does
   not have must not be built as though it had them.
2. ✅ Threaded from `nilbuild` into the interpreter as `--strict-bounds`,
   rather than by setting an environment variable for the child: by §2's own
   rule, semantics must not ride on the environment.
3. ✅ `strictBounds` is an edition property (`VM::setStrictBounds`), derived
   from the edition rather than written in the manifest, so one edition cannot
   mean two things in two projects. `NIL_STRICT_BOUNDS` survives as a
   single-run diagnostic override.

Pinned by `tests/project_edition.cmake`, whose first case is the guarantee
itself: a manifest with no `edition` keeps original semantics, unedited.

Still to do:

4. Record the edition in `.nil` artifacts and in compile-check evidence, so
   Pensieve's recorded repairs state the semantics they were verified under.
   This matters more than it sounds: a repair verified under one edition is
   not evidence for another.
5. Write the compatibility rule into CONTRIBUTING: every change states its
   bucket from §1, and a semantic change without an edition is not merged.
   (Partly done: `tests/repository_policy.py` now requires every design
   record to carry a `**Status:**` line, so a record cannot go stale
   invisibly. NIL-0006 is why — its status stayed accurate while memory of it
   rounded up to "shipped", and the Unix half sat unimplemented for months
   behind a line announcing it was listening.)

**A note from implementing it.** Adding `edition` to `describe` broke
`nilbuild.project-contract`, which asserts the exact key set of
`nil.project.description.v1`. That is the test doing its job — §3 lists
serialized forms as part of the observable surface, and an additive key is
still a change to a contract someone may be matching on. Adding the field was
the right call, but it should be a decision, not a surprise, and the test is
what made it one.
