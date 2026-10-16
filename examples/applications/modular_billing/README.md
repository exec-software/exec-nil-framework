# Modular billing

This two-file application calculates a volume-discounted invoice. `main.ny`
imports the `pricing` module from `pricing.ny` and invokes its public functions
with qualified calls. It exercises source merging, modules, imports,
visibility, qualification, parameters, and return values.

`expected.stdout` and `expected.exit` are the executable contract. Textual NIL,
binary NIL, both bundled routes, LLVM JIT, and LLVM AOT all pass.
