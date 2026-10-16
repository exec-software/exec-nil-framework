# Ny

Ny is the systems-language frontend of NIL Framework. It shares the compiler,
type checker, runtime, standard library, and developer tools with
[Matrix](../matrix/README.md).

- [Language reference](../docs/language-reference/lexical-structure.md)
- [Getting started](../docs/getting-started/index.md)
- [Examples](../examples/README.md)
- [Shared standard library](../nilstd/README.md)
- [Compiler implementation](../nilcompiler/src)

Ny source files retain the `.ny` extension and existing `ny.lang.*` imports.
The compiler command is `nilcompiler`; execution and debugging use
`nilinterpreter` and `nildebugger`. This directory is the entry point for
Ny-specific documentation and future frontend work; shared implementation
remains in the framework components.
