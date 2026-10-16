# NIL-0005: generated non-void fallthrough is explicit

- **Status:** Implemented and verified
- **Date:** 2026-09-14

## Question

How should NIL represent the defensive terminator that the compiler appends
after a non-void function whose source body ends in branching control flow?

## Evidence

An external consumer's complete CLI exposed the ambiguity. `load_regular() -> loaded_file`
returns a value from both arms of an exhaustive `Result` match. The frontend
nevertheless appends a final `RET` after the match-end label. Textual NIL
serialized that instruction identically to a programmer-authored value-less
return, so native lowering had to choose between rejecting a valid aggregate
function and manufacturing an invalid aggregate value.

An earlier in-memory workaround guessed that the last unreachable instruction
was compiler-generated. That guess did not survive serialized control flow:
the defensive match failure edge makes the match-end label structurally
reachable even though a well-typed canonical `Result` can only select one of
the two returning arms. Source locations also are not a semantic identity and
are absent from binary NIL v1.

## Decision

The compiler writes its defensive non-void terminator as:

```text
RET $ny.generated.nonvoid-fallthrough
```

The operand is reserved NIL contract metadata. Text and binary readers accept
only that exact operand; an unknown `RET` operand is malformed NIL. If the
marked path executes, `nilinterpreter` and LLVM-generated programs fail with a
runtime error. They never synthesize zero, a null handle, an enum, or an
aggregate. An ordinary zero-operand `RET` retains the meaning of legacy and
hand-authored NIL, including existing scalar compatibility behavior.

This is a fail-closed extension of executable NIL v1's generic instruction
encoding, not use of a reserved section. An older consumer is expected to
reject a newly generated marked instruction rather than misexecute it.

## Verification

`nil.loader` round-trips the marker through textual and binary NIL, rejects
unknown `RET` operands, and proves that VM execution traps. Route tests prove
both sides of the contract: an exhaustive `Result` match returning a struct
passes textual NIL, binary NIL, both bundles, LLVM JIT, and LLVM AOT; a real
non-void fallthrough fails at runtime on all six routes. The complete
registered LLVM-enabled suite passes 224/224.

## Follow-up

Add source-level control-flow totality and enum-exhaustiveness analysis. Once
the frontend can prove that every non-void path returns, it should reject real
fallthrough before NIL emission and omit the defensive marker when the
match-end path is proven impossible. The marker remains required for older or
conservatively lowered sources and as a cross-process integrity check.
