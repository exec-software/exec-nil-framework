# NIL-0001: Production applications are executable conformance specifications

**Status:** Accepted

**Date:** 2026-09-09

## Context

Ny has a large intended language surface and several execution routes. Small
parser fixtures prove that syntax is accepted, but they do not prove that a
feature survives type checking, NIL serialization, interpretation, bundling,
JIT compilation, native linking, and execution with the same meaning.

Past Ny applications exposed integration defects more effectively than isolated
syntax probes. The approach should be formalized rather than left to memory.

## Decision

Curated, production-shaped applications are part of Ny's conformance suite.
Each application must:

- solve a recognizable problem rather than concatenate unrelated syntax;
- have deterministic inputs, outputs, exit status, and observable effects;
- state the language and library capabilities it covers;
- run through every route that claims to support those capabilities;
- keep route-specific expected failures explicit and temporary; and
- reduce every discovered defect to the smallest useful regression test.

The route harness will preserve intermediate NIL and native artifacts on
failure, identify the failed stage, and compare behavior rather than merely
checking that a process launched.

## Consequences

- Documentation examples should come from passing application code whenever
  practical.
- Backend parity becomes measurable.
- A feature is not described as implemented merely because it parses.
- Experimental AI-native metadata and tracing can be evaluated against real
  programs and real failure investigations.
- The suite will contain both fast focused tests and fewer end-to-end
  applications; neither replaces the other.
