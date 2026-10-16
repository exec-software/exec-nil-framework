# Generic selector

This application selects regional deployment scores through a reusable
generic comparison function, combines the selected values, and emits a deploy
decision. It exercises generic functions, explicit type arguments, generated
specializations, generic parameters, and comparisons.

`expected.stdout` and `expected.exit` are the executable contract. Textual NIL,
binary NIL, both bundled routes, LLVM JIT, and LLVM AOT all pass.
