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
| macOS Intel x86_64 | At `7e53828`, native builds with LLVM ON and OFF passed build/backend-selection gates. Compiler, textual/binary interpreter, Matrix, parser and checkpoint execution passed. LLVM ON also passed assembler/linker contracts, representative JIT/AOT routes and embedded Mach-O context, including dead-strip linking and execution. | The full test inventory still has failures. Installed SDK, debugger/editor desktop acceptance, providers and release qualification remain open. |
| macOS Apple Silicon arm64 | The same `7e53828` build and execution baseline passed, including LLVM-ON native routes and embedded Mach-O context. | The macOS gaps above also apply here; the native inline-assembly profile is currently x86_64-only. |

LLVM-OFF builds exercise frontend/interpreter behavior and rejection of disabled
native operations. Their successful tests do not establish native execution.
The macOS archives contain eleven executable tools per profile, verified as
Mach-O files for the expected architecture; they are build products, not
installed or release-qualified SDK packages.

The exact counts, failures, skips, host provisioning and source identities are
retained in the [Windows/Linux closeout](../notebook/2026-11-02-windows-port.md)
and [macOS build baseline](../notebook/2026-11-04-macos-parity-plan.md).
Subsequent focused portability checks supplement those baselines; they do not
replace the accepted SDK bytes or establish full platform parity.

## Native executable context

Thin Mach-O64 x86_64 and arm64 objects and executables support bounded inspection
and opt-in annotation embedding. Both LLVM-ON hosted profiles at `7e53828` passed
3,592 context assertions and 60 CLI checks, including six native executions
and host-linker dead stripping. LLVM-OFF profiles passed the inspection tests;
they explicitly skip native embedding. See the
[container contract](advanced/executable-context.md) for supported forms and limits.

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

- The full `7e53828` inventory still contains Unicode-fixture, process-cleanup,
  C++ include-path and SDK-publication failures. Follow-up `f8ca6cc` host checks
  pass the five focused filesystem/process suites on both architectures; a new
  full run must confirm their integration. Path spelling and host lookup
  identity differ; see [platform behavior](platform-behavior.md#filesystem-lookup-identity).
- SDK publication still needs an APFS link-lifetime fix. The native diagnostic
  reproduces reader errors when replaced symlink inodes are removed; retained
  link variants pass the bounded stress probe. This is not publication acceptance.
- Native incremental dependency discovery and reuse remain incomplete. Darwin
  compiler loaded-library identity is also incomplete for VM cache reuse;
  successful compilation does not establish reliable incremental reuse.
- The GTK provider currently supports Linux and Windows. macOS UI/provider
  support and live desktop acceptance remain open.
- Full debugger, editor, standard-library/provider and installed-package
  acceptance must follow the shared runtime and process fixes. Shipping a
  Mach-O tool alone does not qualify these surfaces.

The [macOS workflow](../.github/workflows/macos-bringup.yml) records the complete
inventory, including failures and skips. The [release gates](release/v1-profile.md#qualification-and-publication)
still apply before publishing a macOS SDK or declaring parity.
