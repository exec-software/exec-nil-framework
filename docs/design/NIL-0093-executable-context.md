# NIL-0093: executable context is artifact self-description

- **Status:** Implemented within the documented inspection profile
- **Scope:** Shared NIL artifacts produced by Ny and Matrix
- **Recorded:** 2026-10-16

## Decision

An executable artifact may state the intent of its declarations through retained
`@ai_context` annotations. Read-only inspection reports that self-description
without executing the artifact, invoking a model, or treating annotation text
as instructions or authority.

The current `ny.context.v2` manifest includes annotated functions and annotated
struct, class, and enum declarations. Type identity is the emitted kind and
name together. Textual and binary NIL preserve the same annotations. Native
embedding is an explicit option, with inspection supported for the bounded
Linux x86_64 ELF64 little-endian profile.

The existing `ny.context.v2` schema, `.ny.context` native section, and binary
format markers retain their compatibility spellings. Renaming this decision
does not change an artifact format or its reader contract.

## Boundaries

- The manifest contains declared intent, not observed execution behavior.
- Inspection does not resolve or run source capsules or native libraries.
- Embedded annotation text is disclosed verbatim; it is not authenticated or
  automatically scrubbed of secrets.
- Unsupported metadata versions, unknown capability flags, invalid bounds,
  and malformed records fail inspection rather than producing partial claims.
- Runtime state, source maps, reflection snapshots, and analyzer hosting have
  separate contracts.

This record consolidates the implemented contract already documented in
[Inspectable executable context](../advanced/executable-context.md), which
defines the commands, limits, compatibility rules, and current host profile.
The first version's qualification records the initial contract; subsequent
type-annotation coverage
is exercised by the Matrix fixture below.

## Implementation and evidence

- [Context manifest construction](../../nilcompiler/src/executable_context.cpp)
- [NIL loading and binary metadata](../../nilcompiler/src/nil_loader.cpp)
- [Matrix annotation parsing](../../nilcompiler/src/matrix_parser.cpp)
- [Record-context fixture](../../tests/matrix/execution/record-context.matrix)
- [Matrix execution harness](../../tests/matrix/run_execution_tests.py)
- [Native context tests](../../tests/context_manifest_tests.cpp)
