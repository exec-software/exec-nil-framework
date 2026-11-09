# Platform support and validation

Implementation, development acceptance and release qualification are separate.
This page summarizes the recorded development baselines as of 2026-11-05;
the [v1 release profile](release/v1-profile.md) defines the reviewed binary
release boundary. A newer source checkout does not update an installed SDK.

## Recorded platform coverage

| Host | Verified development baseline | Remaining boundary |
| --- | --- | --- |
| Linux x86_64 | Reference host. The clean `5630388` SDK passed the full suite, installed consumer routes, shared runtime, debugger, Unicode/I/O, execution checkpoints and GTK checks. | The reviewed Ubuntu release toolchain is separate from this development SDK. New release binaries still require the release gates. |
| Windows x86_64 | The same `5630388` source has paired Linux acceptance: interpreter/bundles, LLVM JIT/AOT, object/link routes, installed tooling, checkpoints, GTK and VS Code. | Development acceptance on the provisioned Windows VM; bare-host installation and Windows release qualification remain open. GTK and PostgreSQL require external runtimes. |
| macOS 15 Intel x86_64 | At `0c88d2f`, the relocated development SDK passes its installed standard-library campaign, twelve isolated PostgreSQL source/binary routes, installed debugger/checkpoints and corrected numeric-address validation. Earlier LLVM-ON/OFF, native assembly and embedded-context baselines also pass. | Full source suite: 567 passed, one failed and three skipped out of 571. Incremental reuse fails; UI and two native memory-limit tests skip. Desktop/editor and release acceptance remain open. |
| macOS 15 Apple Silicon arm64 | At `3414ce5`, the relocated development SDK passes its installed standard-library campaign, twelve isolated PostgreSQL source/binary routes, installed debugger/checkpoints and corrected numeric-address validation. Native assembly and embedded context also pass. | Full source suite: 567 passed, one failed and three skipped out of 571. Incremental reuse fails; UI and two native memory-limit tests skip. Desktop/editor and release acceptance remain open. |

LLVM-OFF builds exercise frontend/interpreter behavior and rejection of disabled
native operations. Their successful tests do not establish native execution.
The macOS archives contain eleven executable tools per profile, verified as
Mach-O files for the expected architecture. The initial bring-up archives were
build products; the newer SDK workflow additionally stages, relocates and tests
development packages. Neither archive type is a release-qualified SDK.

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
`aarch64` implementation at `f96f237` passes 351 validator checks and eight
actual Ny/Matrix ARM executions through JIT, AOT and text/binary object linking.
Both bounded native assembly profiles are documented in the
[assembly contract](advanced/inline-assembly.md).
Both full LLVM-ON inventories at `f96f237` have 565 passes, one known cache-reuse
failure and five skips out of 571 registrations. Their catalog/registration
timeout mismatch was corrected subsequently. At `2e098f1`, the ARM SDK packages
with exact tested bytes and passes staged and relocated validation, including
an actual shared-runtime ABI/loader check. A diagnostic wording mismatch in the
malformed-library negative control stopped the installed consumer campaign;
full SDK acceptance is still pending.
These source checks do not replace accepted SDK bytes or establish full parity.

The subsequent `2f5d695` SDKs on both architectures each complete all 1,184 reported
installed standard-library routes: 888 retained executions independently checked
from raw evidence, plus 296 object/link routes checked by the pinned campaign driver.
Each private PostgreSQL 16.15 provider passes build/install and canonical loader
checks, followed by six routes each against installed source and binary roots.
The full source inventory has 566 passes, two failures and three skips. The new
failure exposed Darwin accepting scoped IPv6 text as a plain numeric host address;
`8d1f65d` rejects scope suffixes before parsing. Native verification
passes on both architectures at `3414ce5`. The Intel result is retained in a
partial source-test run; it does not establish a complete corrected Intel SDK.
The installed debugger helper stopped before execution on a manifest-field
mismatch. Corrected acceptance at helper revision `12f70cb` subsequently passes
against both original, unchanged SDK artifacts: 146 named debugger checks,
15 JSONL/DAP sessions and the separate live execution-checkpoint contract per host.
This covers interpreter debugging; it does not establish native debugging or
editor GUI acceptance.

The corrected Apple Silicon SDK at `3414ce5` passes 567 source tests, with only
the incremental-reuse failure and the three existing skips. All 1,184 installed
standard-library routes, twelve PostgreSQL routes, 146 debugger checks and the
live checkpoint contract pass again. The matching Intel source-test step reached
its twenty-minute workflow limit before producing a complete report. Packaging
therefore could not start; the corrected numeric-address test did pass beforehand.
The Intel retry at `0c88d2f` subsequently completes in 17 minutes 14 seconds:
567 source tests pass, with the same single incremental-reuse failure and three
skips as Apple Silicon. All 1,184 installed standard-library routes, twelve
PostgreSQL routes, 146 debugger checks and the live checkpoint contract pass.
This corrects the Intel development baseline without replacing accepted SDKs.

A private Windows build at `e087964` also passes four focused compiler-identity,
source-protection and real VM/native incremental contracts: 181 checks, no skips.
This supplements the recorded Windows SDK baseline; it does not replace its
installed binaries or qualify a new complete SDK. A later private build at
`f96f237` passes five focused CTests, including 351 assembly validator checks,
eight native Ny/Matrix executions, the additional Matrix routes and the real
same-process execution-checkpoint contract. The accepted SDK remains unchanged.
A private Windows build at `8d1f65d` also passes both database runtime/client
tests after the numeric-address correction, including IPv4/IPv6 timeout and
socket-close checks. The original checkout, installed SDK and provider DLLs
remain unchanged.

A private Windows build at `8aa60d2` records the first complete local suite since
the port closeout: 574 registered tests against 572 on the Linux reference. The
two inventories share 569 tests. Five are Windows-only: console UTF-8,
unmanifested UTF-8 file paths, the GTK loader, the native memory-limit self-test
and the SDK preflight. Three are Linux-only: toolchain inputs, POSIX process
semantics and a socket-backpressure check of the Python test client rather than
of a product surface. No language, frontend, type-checker, IR, interpreter, LLVM
or standard-library test fails. Four shared contracts fail only when the suite
runs as `SYSTEM`: that account reports `TEMP` cased `C:\WINDOWS\TEMP`, the
toolchain reports paths in their on-disk casing, and the affected assertions
compare the two strings exactly; the same four pass unchanged under an ordinary
user account. The Windows-only SDK preflight needs a machine execution
policy that permits local scripts, because it invokes `powershell.exe -File`
deliberately without a bypass; under the Windows client default of `Restricted`
every subtest observes a policy refusal instead of a preflight diagnostic, and
it passes once the policy is `RemoteSigned`. Two display tests skip for want of
an interactive desktop session and both PostgreSQL tests skip without a
server. This supplements the recorded Windows baseline; it neither
replaces the accepted SDK nor qualifies a new one.

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
  compiler loaded-library identity remains incomplete in production. A separate
  native diagnostic now matches all observed loaded images to ordinary files or
  exact shared-cache mappings on both architectures and hashes every cache file
  twice. Reading roughly 5 GB took 15.057 then 14.056 seconds on ARM, and 104.462
  then 15.351 seconds on Intel. This is not yet a practical cache-key integration;
  native toolchain dependency discovery also remains open. Both VM and native
  builds deliberately rebuild without complete identity.
- A separate [development SDK workflow](../.github/workflows/macos-sdk.yml) now
  implements native Darwin consumer admission, root install layout, pinned
  documentation tooling, and staged dependency/relocation checks. Both
  candidates preserve all 260 payload files plus their manifests through relocation
  and pass the installed standard-library and debugger/checkpoint campaigns.
  The optional isolated PostgreSQL 16 profile now passes
  its coherent client/server and installed source/binary routes on both architectures;
  the scoped-address validation correction passes on both hosts. The complete
  corrected Intel candidate remains pending after a source-campaign timeout.
- The two native lifetime stress tests still lack a demonstrated Darwin memory
  bound. Native diagnostics show that the fixed 96 MiB virtual-memory cap is
  rejected at process setup; larger diagnostic profiles do not replace it.
  Successful lifetime execution routes remain covered by the ordinary suite.
- Desktop UI now uses one provider per platform: GTK on Linux and Win32 on
  Windows. The Windows provider passes the full desktop contract on a real
  interactive session -- provider contract, lifecycle release observation, the
  Matrix desktop suite and the interaction suite that drives real keyboard
  input -- while a session without an interactive window station still skips.
  On macOS the provider is AppKit. Its published contract passes against a
  real window server session on both hosted architectures, 93 checks each,
  and `nil.ui-display` now passes there too: the provider fixture, the
  lifecycle fixture and the Matrix desktop suite. Real keyboard input is
  covered in process, because macOS requires accessibility trust to post
  events to another process and a hosted runner does not grant it; that is a
  limit of cross-process injection, not of the provider, so the interaction
  suite is Linux and Windows only. GTK on macOS is retired, because a GTK
  process there never runs the AppKit event loop and so has no accessibility
  hierarchy at all. Both hosted Mac architectures
  now pass the prerequisite GLib, Cairo-Quartz and HarfBuzz/CoreText compile,
  link and runtime checks, plus exact own-process AppKit key dispatch, view
  delivery and window closure. Directed keyboard input to a verified child and
  native accessibility closure also pass on both hosts. Both have built and
  installed the private GTK dependency stack and passed actual widget input,
  CoreText/font layout, PNG decoding and window teardown checks. Integration
  with the product runtime and language-level desktop acceptance remain open.
- Editor desktop integration, untested provider configurations and release-package
  acceptance remain separate from the passing installed campaigns. Shipping a
  Mach-O tool alone does not qualify these surfaces.

The [macOS workflow](../.github/workflows/macos-bringup.yml) records the complete
inventory, including failures and skips. The [release gates](release/v1-profile.md#qualification-and-publication)
still apply before publishing a macOS SDK or declaring parity.
