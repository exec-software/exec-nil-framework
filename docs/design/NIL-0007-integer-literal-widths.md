# NIL-0007: Integer literal widths and range validation

**Status:** Implemented.

**Scope:** Shared Ny/Matrix literal semantics, array inference and diagnostics.

## Problem

Every integer literal previously inferred `i32`, including values that need
64 bits. A large literal could print correctly through the VM's value carrier
yet truncate when stored or used in arithmetic. The expression
`-9223372036854775807 - 1` consequently evaluated to zero before conversion to
an `i64` parameter. Array inference also retained the first element's type,
allowing a later `i64` value to narrow silently into an inferred `array<i32>`.
This also broke the standard library's `abs_i64` overflow guard: zero panicked
and the minimum signed value wrapped instead of reporting overflow.

## Decision

Decimal integer literals use `i32` when representable and `i64` otherwise,
within the signed 64-bit range. Negated literal boundary values support
`-2147483648` as `i32` and `-9223372036854775808` as `i64` without decoding an
unrepresentable positive intermediate. Values outside that range are source
errors, including in editor analysis and when type checking is disabled.

Hexadecimal literals retain their established mask behavior: values through
`0xFFFFFFFF` infer `i32`, while larger supported values infer `i64`. An
`i32` mask is interpreted at 32 bits when stored or used by typed arithmetic;
an explicit `0xFFFFFFFF as i64` retains the positive value `4294967295`.
Positive hexadecimal values above `0x7FFFFFFFFFFFFFFF` are rejected. Negating
`0x8000000000000000` can represent the signed 64-bit minimum; this does not
introduce unsigned 64-bit values.

Inferred integer arrays join `i32` and `i64` elements to `i64`, in either order.
This applies to typed variables as well as literal elements. It does not make
`array<i32>` and `array<i64>` interchangeable; callers must still supply the
declared element type at an array boundary.

## Compatibility and limits

Ordinary arithmetic still uses operand types and wraps at its resolved width.
`2147483647 + 1` remains `i32` arithmetic. An outer `i64` destination or
parameter does not widen that inner operation. Explicit scalar casts and
storage conversions continue to narrow by retaining low bits.

Existing programs that relied on implicit narrowing of large decimal literals
must state the intended `i32` conversion. Existing 32-bit hexadecimal masks
retain their width. The two frontends share these rules; Matrix's intended
universal `number` default and broader contextual expression typing remain
separate decisions. `--no-typecheck` retains its legacy untyped arithmetic
contract; range validation is still mandatory.

## Verification

Paired source fixtures exercise scalar boundaries, both forms of match,
globals, nested calls, explicit wrapping, hexadecimal masks and integer arrays.
Core-library checks cover `abs_i64(0)`, wide negative inputs and rejection of
the signed minimum on both frontends and all enabled execution routes.
Rejection tests cover ordinary expressions, patterns, unused defaults, editor
analysis, unchecked compilation and preservation of existing output files.
Qualify both frontends through textual/binary NIL, bundles, LLVM JIT/AOT and
the assembler/linker pipelines, including installed source/binary libraries.
