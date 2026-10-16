# Recovery baseline — 2026-09-09

**Status:** Historical recovery snapshot; live gap list superseded

> **Status:** Historical recovery snapshot; the live gap list it carried is
> superseded by the current source and test suite.
> Binary NIL/function/opcode parity, globals, range `continue`, variant
> spelling, floating comparisons, and other items below changed during the
> cleanup pass. This file preserves the order in which problems were discovered;
> it is not the current backlog.

## Question

What can the recovered Ny repository honestly claim before feature work
resumes?

## Repository state

The cleanup establishes canonical `ny*` component names, a root CMake build,
an explicit experimental quarantine for unfinished library sources, and current
project documentation. LLVM native compilation remains an active execution
route.

The central `/home/execute/exec/notebook` Obsidian vault contained no Ny
notebook by filename, content, symlink, notebook extension, or local Git
history. This project-local notebook was therefore created as the canonical Ny
notebook rather than moving unrelated material.

## Verified baseline

- The SDK builds with LLVM enabled and disabled.
- The registered smoke suite passes.
- A minimal program compiles to textual NIL and executes in `nilinterpreter`.
- A minimal program compiles to binary NIL and executes in `nilinterpreter`.
- A minimal bundled executable runs.
- A minimal LLVM JIT program runs.
- All 99 active standard-library candidates survive parsing/code generation
  with type checking disabled; 20 passed the initial full-type-check screen.

The later current-state audit measures 57 independent full-type-check passes.

The last standard-library number is an inventory result, not a functionality
claim.

## Historical high-priority implementation gaps (superseded)

1. Binary NIL loading discards function identity and silently omits opcodes.
2. Unsupported NIL and LLVM instructions can become no-ops instead of errors.
3. `attempt`/`fault`/`recover`, postfix `?`, assertions, and hot-reload syntax
   are accepted by the frontend but are not functionally lowered.
4. Lambdas, default arguments, references, some generic methods, and class
   methods do not establish correct call-frame parameters.
5. Textual NIL, binary NIL, the VM, LLVM JIT, and native AOT do not yet share a
   verified opcode/semantic contract.
6. Several documented features are design syntax rather than completed syntax,
   including range expressions, advanced traits/classes, runnable property
   tests, and parts of `Option`/`Result`.

## Working implementation boundary at recovery time

At recovery time, textual NIL plus `nilinterpreter` was the broadest reliable
route. Verified language slices included primitive operations, core control
flow, named functions and recursion, dynamic arrays, structs and concrete
`impl` methods, explicit generic specialization, basic concrete traits, enums and simple
matching, multi-file modules, and allocation-based pointer operations.

This boundary contains defects: range-loop `continue` can loop forever,
overload dispatch is name-only, inferred generics are unreliable, and several
apparently accepted constructs fail at runtime.

## Decision

Stabilization will proceed vertically through production-style applications.
Each application owns deterministic output and runs through every applicable
route. Missing behavior becomes a focused regression test; unsupported routes
must fail clearly instead of reporting false success.

The first application set is:

1. backend and packaging hello;
2. language fundamentals/checksum;
3. data-model/inventory workflow;
4. multi-file modular application; and
5. manual-memory record store.

Interpreter and backend orchestration belongs in a reusable route harness, not
in ad-hoc shell history.

## Immediate follow-up

Preserve this cleanup as a baseline commit, then fix binary NIL function/opcode
parity before broadening the application matrix. Add expected-failure cases for
known gaps so progress is visible without weakening the passing suite.
