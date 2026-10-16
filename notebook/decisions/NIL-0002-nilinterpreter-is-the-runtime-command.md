# NIL-0002: `nilinterpreter` is the runtime command

- **Status:** Implemented
- **Date:** 2026-09-09

## Question

What is the canonical public name for the executable that loads and executes
textual or binary NIL?

## Evidence

The repository's initial commit (`e0c5f62`) built the source now used by the
NIL runtime with the output name `nilinterpreter`; its compiler CMake file even
described the previous `vessel` target as a legacy name. A later, incomplete
directory and target rename allowed an internal implementation term to escape
into command names, documentation, tests, and editor settings.

The execution core is still implemented by the C++ `ny::VM` class in
`nilcompiler/src/vm.*`. That class name describes an internal bytecode
engine; it does not define the SDK's public command name.

## Decision

`nilinterpreter` is the only public command and component name for NIL
interpretation. Its source lives in `nilinterpreter`, its CMake target is
`nilinterpreter`, and integrations use terms such as `interpreterPath` or
`ny.interpreter.path`.

Internal engine identifiers may remain until a separate architectural refactor
has a concrete benefit. Protocol method names in the existing experimental
`vm.*` namespace also remain compatible until that protocol is versioned; they
must be described as legacy protocol identifiers, not as the product name.

## Guardrail

CI and review should reject reintroduction of the superseded public component
name. Route tests must call their backends `text-interpreter` and
`binary-interpreter` so reports reflect the supported tool boundary.

## Follow-up

Keep `ny.repository-policy` in the required CTest baseline. It scans public
paths, CMake targets, documentation, task definitions, extension settings, and
Ny sources so the retired name and conversion feature cannot silently return.
