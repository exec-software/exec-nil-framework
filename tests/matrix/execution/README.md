# Matrix execution fixtures

Each `*.matrix` page compiles to NIL and runs on `nilinterpreter`; its output
must match the matching `*.expected` byte for byte. Driven by
`tests/matrix/run_execution_tests.py` and registered as the `matrix.execution`
CTest.

`scalar-subset-in-ny.ny` is the same program written by hand in Ny. Its output
is identical to the Matrix version, including the path. The source spellings
differ:

```text
Matrix:  "C:\dir\file.txt"
Ny:      "C:\\dir\\file.txt"
```

Matrix text literals have no escape sequences, so the path means what it says.
Ny uses doubled backslashes for literal backslashes; `\f` now correctly denotes
form feed. The former fixture used unsupported escapes and expected lost
backslashes, accidentally relying on a decoder defect. Unknown-escape
diagnostics remain tracked in the living findings. This pair now verifies the
documented source spelling for the same text value on both surfaces.

`faults-twin.ny` is the same program again, and it carries a different kind of
evidence: fault payloads have **nominal** identity, so a matching struct
section means an Ny `raise ReadFault` is caught by a Matrix
`caught ReadFault` handler and neither language has a private copy of the
type. `run_execution_tests.py` compares struct, enum and class records
byte for byte whenever a twin exists.

## Fixtures that are samples, and the route suite

`faults.matrix`, `memory.matrix`, `parallel-time.matrix` and
`context-interop.matrix` are the normative samples 08–10 and 13, copied here
so the interpreter fixture format applies to them unchanged. Their siblings in
[`../routes/`](../routes/) run the same samples through
`tests/routes/ny_route_matrix.py` on all six execution routes
(`matrix.routes.*`), and pin the inline-assembly split: the interpreted
routes must reject a reached assembly operation with a native-route
diagnostic while the LLVM routes execute it.
