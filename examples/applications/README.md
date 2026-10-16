# Ny application conformance suite

These are executable applications, not parser-only syntax samples. Each one
has an exact `expected.stdout`, an exact `expected.exit`, and a route contract
in `suite.json`. The suite runner delegates every route to the auditable
`tests/routes/ny_route_matrix.py` harness. The release qualifier also asserts
that its AI context annotation survives in textual NIL, so annotation coverage
is an artifact contract rather than a source-only claim.

## Coverage

| Application | Working language slice | Current route contract |
| --- | --- | --- |
| `release_qualifier` | AI context annotation, functions, integer arithmetic, mutable locals, while loops, nested conditionals | All six routes pass |
| `capacity_planner` | Recursion, parameters, globals, range loops, `continue`, while loops | All six routes pass |
| `telemetry_rollup` | Array literals, indexing, indexed assignment, aggregation | All six routes pass |
| `dispatch_board` | Enums, associated values, match, pattern bindings | All six routes pass |
| `inventory_model` | Structs, field access/mutation, trait declarations, impl blocks, concrete methods | All six routes pass |
| `manual_buffer` | Typed pointers, allocation, indexing, deallocation | All six routes pass |
| `modular_billing` | Multiple files, modules, imports, qualified calls | All six routes pass |
| `generic_selector` | Generic functions, explicit type arguments, specialization | All six routes pass |

All eight applications now require successful execution on all six routes;
there are no expected failures in the current suite manifest. The separate
language route matrix retains explicit rejection contracts for unsupported or
invalid constructs. A passed rejection test is not implementation evidence.

The suite does not claim that classes, lambdas, fault propagation, assertions,
or every combination of generics and modules is complete. Dedicated trait
conformance and associated-call contracts live in the language/tool tests;
this small application suite is not their exhaustive oracle.

## Run the suite

Build Ny with LLVM enabled, then run from the repository root:

```bash
python3 examples/applications/run_suite.py \
  --compiler sdk/nilcompiler \
  --interpreter sdk/nilinterpreter
```

The default is all six routes:

- textual NIL through `nilinterpreter`;
- binary NIL through `nilinterpreter`;
- bundled textual NIL;
- bundled binary NIL;
- LLVM JIT;
- LLVM AOT native executable.

Reports are written under `build/application-conformance/`. Per-run artifacts
are removed after all route and textual-NIL contract checks pass, including
expected failures. Failed application artifacts are preserved under `work/`
and their exact directory is printed for diagnosis. A nonzero suite exit means
a regression, an unexpected pass, or a requested route that could not be
exercised.

For a focused interpreter-only development pass:

```bash
python3 examples/applications/run_suite.py \
  --compiler sdk/nilcompiler \
  --interpreter sdk/nilinterpreter \
  --routes text-interpreter,binary-interpreter
```
