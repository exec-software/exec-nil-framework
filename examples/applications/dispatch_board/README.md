# Dispatch board

This application models queued, running, completed, and failed jobs and
extracts each state payload with pattern matching. It exercises enums,
associated values, match statements, and pattern bindings.

`expected.stdout` and `expected.exit` are the executable contract. Textual NIL,
binary NIL, both bundled routes, LLVM JIT, and LLVM AOT all pass.
