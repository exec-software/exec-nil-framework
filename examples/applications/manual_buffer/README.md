# Manual buffer

This application allocates a typed integer buffer, populates it, calculates a
checksum, reports boundary values, and releases the allocation. It exercises
typed pointers, `allocate`, pointer indexing and assignment, loops, and
`deallocate`.

`expected.stdout` and `expected.exit` are the executable contract. Textual and
binary NIL, both bundled routes, LLVM JIT, and LLVM AOT all pass.
