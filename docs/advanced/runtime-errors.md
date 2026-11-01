# Runtime errors: what a NIL program tells you when it goes wrong

Written 2026-11-01 after measuring the actual behaviour rather than reading the
intent. The compile-time diagnostics are good — named codes, a source location,
and often a suggested fix:

```
src/main.matrix:8:22: error MX0103: 'list' is not how Matrix spells this
  use 'array of <type>'
```

The runtime side is not yet at that standard, and this page records exactly
where it stands so the gap is visible rather than surprising.

## What surfaces today

| condition | exit | what you see |
| --- | --- | --- |
| divide / modulo by zero | 1 | `NIL interpreter runtime error: Division by zero` |
| uncaught language fault | 1 | `Ny uncaught fault: <type> at line <N>` |
| array index out of range | **0** | **nothing** — see below |

Two gaps follow from that table.

**Runtime errors carry far less than compile errors do.** A fault gives a line
but no file and no stack; a generic runtime error gives neither. Compare the
compile diagnostic above. There is no reason the runtime cannot reach the same
standard, and the fault path already proves the line number is available.

**An out-of-bounds array access is silent by design.** A read past the end
yields the element default, a write past the end is dropped, and the length
never changes. This is deliberate and specified — see
`tests/language/array-bounds-consistency.ny`, which pins it — so it is a
contract, not a bug. It is named *consistency* because the alternative
considered was undefined behaviour.

But it is a hard contract to debug against. A program that writes past the end
of an array keeps running with a value nobody wrote, and the first visible
symptom appears somewhere else entirely, often much later. "It crashes and I
get no error" is the usual report, and the crash is rarely where the fault is.

## Strict bounds, for when you are hunting one of those

```sh
NIL_STRICT_BOUNDS=1 nilinterpreter program.nil
NIL_STRICT_BOUNDS=1 nilbuild run
```

With the variable set, the same access reports instead of passing silently:

```
NIL interpreter runtime error: array index 99 is out of bounds at line 5:
cannot read from an array of 3 elements, so the valid indices are 0 to 2
```

The message names the index, the line, the operation, the actual length and
the range that would have been valid — which between them usually identify the
bug without a debugger.

Notes on scope:

- **Opt-in, and the default is untouched.** Turning this on by default would
  change the meaning of every NIL program ever written. That is a language
  decision, not a diagnostic one, so it is not made here.
- **VM only.** The check lives in the VM's `GET_INDEX` and `SET_INDEX`. The
  LLVM backend has its own bounds path and is unaffected.
- Pinned by `tests/language/array-bounds-strict.ny`, registered beside the
  consistency test so the two cannot drift apart unnoticed.

## Crash artifacts

Every uncaught runtime error now writes a `.nycrash` report, automatically,
with no change to the program and nothing to import:

```
[meta]
format = nycrash/1
source = interpreter_self
nil_version = 1.0.0-rc.1
pid = 2975478

[error]
type = NIL interpreter runtime error
message = array index 99 is out of bounds at line 5: cannot read from an
          array of 3 elements, so the valid indices are 0 to 2
exit_code = 1

[location]
program = /path/to/program.nil
line = 5
```

The format is the existing `nycrash/1` that `nilaix/src/ny_crash.ny` writes, so
the Enigma `parse_nycrash` reader needs no changes. `source = interpreter_self`
is a new value distinguishing a report the interpreter wrote about itself from
one a host scraped out of stderr.

| variable | effect |
| --- | --- |
| *(unset)* | report written; **stderr unchanged** |
| `NIL_CRASH_REPORT=1` | also prints `[nilcrash] wrote <path>` |
| `NIL_CRASH_REPORT=0` | writes nothing |
| `NIL_CRASH_DIR=<dir>` | writes there instead of the working directory, and prints the path |

The split between writing the file and printing the notice is deliberate. The
file breaks nothing. The extra stderr line broke six route tests the first time
it was tried, because a crashing program's stderr is something tools parse — so
the notice is tied to having asked. See NIL-0026 for the general rule.

Reports are named `<program>-<pid>.nycrash` and are not cleaned up; they
accumulate until deleted.

**Why this did not exist before:** `ny_crash.ny` has been able to write these
for a long time, but its `write_interpreter` entry point was designed to be
called by a *host* process that captured a dying child's stderr, and no such
host was ever written. The machinery existed and never fired. Writing the
report from the interpreter removes the need for a host at all.

## Worth doing next

Ordered by how much they would have helped on a real hunt:

1. **A stack trace on every uncaught error.** The interpreter has frames; the
   top-level handler in `nilinterpreter/src/main.cpp` prints only `e.what()`.
2. **File and column, not just a line.** The fault carries `source_line`
   alone. Compile diagnostics already carry `file:line:col`.
3. **A diagnostic code per runtime error**, matching the `MX####` / `NIL####`
   scheme, so a runtime failure is as searchable as a compile failure.
4. **Surface the same errors over NIL-0006**, so an attached agent is told
   about a fault rather than only seeing the process exit. The crash artifact
   now exists; an attached agent still has to notice the process died and go
   looking for it.
5. **Carry the source line structurally** through the bounds path. The message
   contains `at line N`, but it reaches the reporter as prose, so the
   artifact's `[location] line` field is only populated for language faults.
