# Telemetry rollup

This application repairs one sensor reading, then computes a total, minimum,
maximum, alert count, and terminal sample over a fixed telemetry window. It
exercises array literals, indexed reads, indexed writes, range loops, and
aggregation control flow.

`expected.stdout` and `expected.exit` are the executable contract. Textual NIL,
binary NIL, both bundled routes, LLVM JIT, and LLVM AOT all pass.
