# NIL Framework documentation

NIL Framework provides the shared compiler, runtime, library, and developer
tools for Ny and Matrix. Both languages compile to NIL and use the same VM
and native backends.

This documentation covers the first-release candidate and explicitly identified
experiments. The [v1 release profile](release/v1-profile.md) defines the candidate's
supported host and feature boundary. Some design pages describe future work;
they do not extend that profile. Report disagreements between the reference,
command-line help, and executable tests as bugs.

## Start here

- [v1 release profile](release/v1-profile.md) — supported scope, installation,
  compatibility boundaries, and publication gates.
- [Getting started](getting-started/index.md) — build the SDK, compile a minimal
  program, and run NIL in `nilinterpreter`.
- [Tool reference](tools/index.md) — current command lines and explicit prototype
  boundaries.
- [Projects and analysis](tools/projects.md) — one configuration for checking,
  VM/native builds, running and live editor analysis.
- [Developer tooling](developer-tooling.md) — current editor support and the
  sequence toward shared workspace semantics.
- [Standard-library status](standard-library/index.md) — layout, maturity, and
  what is deliberately outside the core library.
- [NILData](standard-library/nildata.md) — structured data, JSON interchange and explicit NyData migration.
- [Examples](../examples/README.md) — small examples tied to repository fixtures.
- [Matrix](../matrix/README.md) — prose-oriented frontend, syntax, and examples.
- [Ny](../ny/README.md) — systems-language frontend and reference entry point.

## Language reference

The reference describes Ny syntax and direction. Coverage varies by feature;
do not assume every documented combination is accepted by every backend yet.

- [Lexical structure](language-reference/lexical-structure.md)
- [Types](language-reference/types.md)
- [Classes and polymorphism](language-reference/classes.md)
- [Expressions](language-reference/expressions.md)
- [Statements](language-reference/statements.md)
- [Faults and cleanup](language-reference/faults.md)
- [Functions](language-reference/functions.md)
- [Modules](language-reference/modules.md)
- [Traits](language-reference/traits.md)

## Architecture and design

- [Advanced implementation topics](advanced/index.md)
- [Design documents](design/README.md)
- [NIL-0006: AI control module](design/NIL-0006-ai-module.md)
- [nypm package-manager proposal](design/nypm.md)

## Source map

The canonical component directories are directly under the project root: `nilcompiler`, `nilinterpreter`,
`nildebugger`, `nillsp`, `nilstd`, `nilaix`, `nilassembler`, `nillinker`,
`nilschema`, `nilagent`, and `nilbuild`. Shared tooling utilities live in `common`.
First-party package experiments live under
`packages`.
