# Release qualifier

This application evaluates a deterministic batch of service latencies and a
backlog drain before printing release-gate decisions. It exercises
AI context annotations, parameterless functions, `i64` arithmetic, mutable
locals, `while` loops, conditionals, and nested control flow.

`expected.stdout` and `expected.exit` are the executable contract. The program
currently passes textual NIL, binary NIL, both bundled routes, LLVM JIT, and
LLVM AOT. The suite additionally checks that `@ai_context` is retained in the
textual NIL artifact.
