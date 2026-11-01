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

## Worth doing next

Ordered by how much they would have helped on a real hunt:

1. **A stack trace on every uncaught error.** The interpreter has frames; the
   top-level handler in `nilinterpreter/src/main.cpp` prints only `e.what()`.
2. **File and column, not just a line.** The fault carries `source_line`
   alone. Compile diagnostics already carry `file:line:col`.
3. **A diagnostic code per runtime error**, matching the `MX####` / `NIL####`
   scheme, so a runtime failure is as searchable as a compile failure.
4. **Automatic crash artifacts.** `nilaix/src/ny_crash.ny` can already write
   `.nycrash` reports, but nothing calls it automatically — so the machinery
   exists and never fires. Wiring it to the top-level handler would mean every
   crash left a readable artifact without the program opting in.
5. **Surface the same errors over NIL-0006**, so an attached agent is told
   about a fault rather than only seeing the process exit.
