# Capacity planner

This application forecasts eight scheduling slots, excludes a maintenance
window, counts urgent capacity, and normalizes the resulting score. It
exercises recursion, parameterized calls, mutable global state, range loops,
`continue`, conditionals, and while loops.

`expected.stdout` and `expected.exit` are the executable contract. Textual NIL,
binary NIL, both bundled routes, LLVM JIT, and LLVM AOT all pass.
