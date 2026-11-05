# Platform support and validation

Implementation, development acceptance and release qualification are separate.
This page summarizes the recorded development baselines as of 2026-11-04;
the [v1 release profile](release/v1-profile.md) defines the reviewed binary
release boundary. A newer source checkout does not update an installed SDK.

## Recorded platform coverage

| Host | Verified development baseline | Remaining boundary |
| --- | --- | --- |
| Linux x86_64 | Reference host. The clean `5630388` SDK passed the full suite, installed consumer routes, shared runtime, debugger, Unicode/I/O, execution checkpoints and GTK checks. | The reviewed Ubuntu release toolchain is separate from this development SDK. New release binaries still require the release gates. |
| Windows x86_64 | The same `5630388` source has paired Linux acceptance: interpreter/bundles, LLVM JIT/AOT, object/link routes, installed tooling, checkpoints, GTK and VS Code. | Development acceptance on the provisioned Windows VM; bare-host installation and Windows release qualification remain open. GTK and PostgreSQL require external runtimes. |
| macOS 15 Intel x86_64 | At `e087964`, native builds with LLVM ON and OFF passed build/backend-selection gates. Compiler, textual/binary interpreter, Matrix, parser and checkpoint execution passed. LLVM ON also passed assembler/linker contracts, representative JIT/AOT routes and embedded Mach-O context, including dead-strip linking and execution. | The full inventory fails only its positive incremental-reuse contract. Installed SDK, debugger/editor desktop acceptance, providers and release qualification remain open. |
| macOS 15 Apple Silicon arm64 | The same `e087964` build and execution baseline passed, including LLVM-ON native routes and embedded Mach-O context. | The macOS gaps above also apply here. A bounded AArch64 inline-assembly profile has since been implemented; native ARM acceptance is pending. |

LLVM-OFF builds exercise frontend/interpreter behavior and rejection of disabled
native operations. Their successful tests do not establish native execution.
The macOS archives contain eleven executable tools per profile, verified as
Mach-O files for the expected architecture; they are build products, not
installed or release-qualified SDK packages.

The exact counts, failures, skips, host provisioning and source identities are
retained in the [Windows/Linux closeout](../notebook/2026-11-02-windows-port.md)
and [macOS build baseline](../notebook/2026-11-04-macos-parity-plan.md).
At `e087964`, the full macOS inventories are complete and all build/backend gates
pass. Failures and skips retain their actual status:

| Profile | Pass | Fail | Skip | Registered |
| --- | ---: | ---: | ---: | ---: |
| Intel, LLVM ON | 564 | 1 | 5 | 570 |
| Intel, LLVM OFF | 419 | 1 | 146 | 566 |
| Apple Silicon, LLVM ON | 563 | 2 | 5 | 570 |
| Apple Silicon, LLVM OFF | 419 | 1 | 146 | 566 |

All four pass the filesystem, Unicode namespace, owned-process, parallel-process
and SDK-publication contracts. The new compiler-identity checks pass while the
positive cache-reuse test continues to fail on Darwin. Apple Silicon also fails
the x86_64 native-assembly fixture in that baseline. The subsequent bounded
`aarch64` implementation passes 351 validator checks and Linux regression tests,
including foreign-target rejection; its actual ARM execution is still pending.
These source checks do not replace accepted SDK bytes or establish full parity.

A private Windows build at `e087964` also passes four focused compiler-identity,
source-protection and real VM/native incremental contracts: 181 checks, no skips.
This supplements the recorded Windows SDK baseline; it does not replace its
installed binaries or qualify a new complete SDK.

## Native executable context

Thin Mach-O64 x86_64 and arm64 objects and executables support bounded inspection
and opt-in annotation embedding. Both LLVM-ON hosted profiles at `e087964` passed
3,592 context assertions and 60 CLI checks, including six native executions
and host-linker dead stripping. LLVM-OFF profiles passed the inspection tests;
they explicitly skip native embedding. See the
[container contract](advanced/executable-context.md) for supported forms and limits.

## SDK publication mechanics

The full `e087964` matrix passes the publication contract on all four profiles.
A separate native stress check also passes 5,000 replacements on each architecture.
Darwin retains old symlink inodes and preserves readable new links under
restrictive umasks. This verifies publication mechanics, not a complete installed
SDK. See [SDK publication](getting-started/index.md) for retention lifetime.

## Runtime control and checkpoints

`nilinterpreter --ai` provides opt-in local control through Windows named pipes
or Unix sockets. Pause, resume, instruction stepping and bounded state inspection
use the real VM execution thread. `--ai-wait` starts paused before `main`.
The live checkpoint contract passed on both Windows/Linux development SDKs and
all four recorded macOS profiles.

`vm.snapshot` and `vm.restore` provide reusable **same-process** execution
checkpoints for a paused, deterministic VM program. They capture execution
position, frames, values, heap identities and admitted runtime state. The
standalone interpreter retains the latest successful checkpoint. External
effects, open resources, native/JIT execution, cross-process persistence and
code hot reload are outside this profile. Programs containing excluded
operations are rejected before capture; this is not arbitrary application
rollback. See [NIL-0006](design/NIL-0006-ai-module.md) for commands, limits and
the complete admission rules.

## Remaining macOS work

- Native incremental dependency discovery and reuse remain incomplete. Darwin
  compiler loaded-library identity is also incomplete. Both VM and native builds
  now deliberately rebuild rather than reuse artifacts without complete identity;
  successful compilation does not establish incremental parity.
- The bounded ARM inline-assembly profile and host-selected native route checks
  are implemented but await native ARM acceptance. Intel x86_64 native assembly
  passes. See the [assembly contract](advanced/inline-assembly.md) for both profiles.
- SDK packaging needs Darwin admission in the consumer route gate, root install
  layout, documentation tooling, and staged dependency/relocation validation.
  The current workflow archives build products rather than an installed SDK.
- The GTK provider currently supports Linux and Windows. macOS UI/provider
  support and live desktop acceptance remain open.
- Full debugger, editor, standard-library/provider and installed-package
  acceptance remains separate from the passing runtime/process contracts. Shipping a
  Mach-O tool alone does not qualify these surfaces.

The [macOS workflow](../.github/workflows/macos-bringup.yml) records the complete
inventory, including failures and skips. The [release gates](release/v1-profile.md#qualification-and-publication)
still apply before publishing a macOS SDK or declaring parity.
