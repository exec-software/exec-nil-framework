# Ny execution-route matrix

`ny_route_matrix.py` treats compilation, artifact validation, and execution as
separate checks. A route does not pass merely because a command exits zero.

## Execution routes

| Route | Pipeline |
| --- | --- |
| `text-interpreter` | Ny source -> textual NIL -> `nilinterpreter` |
| `binary-interpreter` | Ny source -> binary NIL -> `nilinterpreter` |
| `bundle-text` | Ny source -> textual NIL embedded in a copied `nilinterpreter` |
| `bundle-binary` | Ny source -> binary NIL embedded in a copied `nilinterpreter` |
| `llvm-jit` | Ny source -> NIL module -> in-process LLVM JIT |
| `llvm-aot` | Ny source -> NIL module -> native object/link -> executable |

`compile-reject` and `interpreter-reject` are additional negative-input routes.
They verify that invalid source is rejected by the compiler and that source,
malformed NIL, unknown opcodes, or missing entry points are rejected by the
interpreter.

## Basic invocation

From the repository root:

```bash
python3 tests/routes/ny_route_matrix.py \
  --compiler build/cmake/bin/nilcompiler \
  --interpreter build/cmake/bin/nilinterpreter \
  --source tests/routes/route_smoke.ny \
  --expected-stdout tests/routes/route_smoke.stdout \
  --expected-exit 0 \
  --routes all \
  --work-dir build/route-matrix \
  --report-json build/route-matrix/route-smoke.json \
  --strict
```

The report records every command, exit status, captured stream, artifact hash,
format check, timeout, and route verdict. Work directories are unique. They are
removed after a pass, expected failure (`XFAIL`), or expected skip, and are
preserved with a diagnostic after `FAIL`, `XPASS`, or a strict skip. Put
`--report-json` outside `--work-dir` when a successful run's JSON report must be
retained; the default report is removed with the successful run directory.
Successful compile stages must be silent under
`--quiet`, and execution stderr must be empty unless an exact
`--expected-stderr` fixture is supplied; backend errors cannot coexist with a
passing route.

LLVM routes automatically become `SKIP` when the compiler says that LLVM is not
available. A run containing only skipped routes exits `77`; `--strict` makes any
requested skip fail the aggregate run.

Run the global-storage fixture through the native routes with:

```bash
python3 tests/routes/ny_route_matrix.py \
  --compiler build/cmake/bin/nilcompiler \
  --interpreter build/cmake/bin/nilinterpreter \
  --source tests/routes/globals.ny \
  --expected-stdout tests/routes/globals.stdout \
  --expected-exit 0 \
  --routes llvm-jit,llvm-aot \
  --strict
```

Known defects should remain visible as expected failures rather than silently
accepted. An expected failure is `XFAIL` and does not fail the harness. If it
unexpectedly starts passing, it becomes `XPASS` and the harness exits nonzero
so the obsolete allowance is removed deliberately. Explicit skips use the same
syntax with `--skip ROUTE=REASON`. Use
`--xfail-stderr-contains ROUTE=FRAGMENT` with a matching `--xfail` when the
failure mode itself is part of the contract; this prevents an unrelated crash
or diagnostic from satisfying the expected failure.

## Initial contracts

The common route smoke is expected to pass through all six routes:

```bash
python3 tests/routes/ny_route_matrix.py \
  --compiler build/cmake/bin/nilcompiler \
  --interpreter build/cmake/bin/nilinterpreter \
  --source tests/routes/route_smoke.ny \
  --expected-stdout tests/routes/route_smoke.stdout \
  --expected-exit 0 \
  --routes all \
  --strict
```

The JIT is an in-process execution route. The harness validates its exact
process result and also validates the lowered textual NIL when that optional
intermediate is retained. AOT must produce and execute a recognizable native
executable.

`return_seven.ny` checks program exit-code propagation through every route:

```bash
python3 tests/routes/ny_route_matrix.py \
  --compiler build/cmake/bin/nilcompiler \
  --interpreter build/cmake/bin/nilinterpreter \
  --source tests/routes/return_seven.ny \
  --expected-stdout tests/routes/return_seven.stdout \
  --expected-exit 7 \
  --routes all \
  --strict
```

`globals.ny` catches opcode-table divergence rather than only exercising the
lowest common denominator. Run a focused runtime-error contract with:

```bash
python3 tests/routes/ny_route_matrix.py \
  --compiler build/cmake/bin/nilcompiler \
  --interpreter build/cmake/bin/nilinterpreter \
  --source tests/routes/runtime_error.ny \
  --expected-stdout tests/routes/empty.stdout \
  --expected-stderr tests/routes/runtime_error.stderr \
  --expected-exit 1 \
  --routes all
```

The negative routes use `empty.stdout`. For example, this verifies that
`nilinterpreter` rejects Ny source rather than treating it as textual NIL:

```bash
python3 tests/routes/ny_route_matrix.py \
  --compiler build/cmake/bin/nilcompiler \
  --interpreter build/cmake/bin/nilinterpreter \
  --source tests/routes/route_smoke.ny \
  --expected-stdout tests/routes/empty.stdout \
  --expected-stderr tests/routes/source_input.stderr \
  --expected-exit 1 \
  --routes interpreter-reject \
  --strict
```

The other interpreter-rejection inputs have matching exact-error fixtures for
invalid structure, unknown instructions, missing/false entry points, duplicate
labels, missing operands, invalid `main` parameters, and signed division or
remainder overflow. Compiler-rejection fixtures cover invalid syntax, invalid
function arity, omitted default arguments, invalid `main` declarations, inline
assembly, unavailable fault/propagation/compiler intrinsics, structured match
payloads, and other AST forms without NIL lowering.
An expected stderr file may contain `{{SOURCE}}`; the harness replaces that
token with the resolved input path before performing the exact comparison.
