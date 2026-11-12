# Platform support and validation

Implementation, development acceptance and release qualification are separate.
This page summarizes the recorded development baselines as of 2026-11-11;
the [v1 release profile](release/v1-profile.md) defines the reviewed binary
release boundary. A newer source checkout does not update an installed SDK.

## Recorded platform coverage

| Host | Verified development baseline | Remaining boundary |
| --- | --- | --- |
| Linux x86_64 | Reference host. The clean `5630388` SDK passed installed consumer routes, shared runtime, debugger, Unicode/I/O, checkpoints and GTK checks. The newer `1efbe66` LLVM-ON source suite passes all 589 CTests without skips. | The reviewed Ubuntu release toolchain is separate from this development SDK. New release binaries still require the release gates. |
| Windows x86_64 | The `5630388` installed SDK retains paired Linux acceptance. The newer private `f79b94b` source build passes 579 of 583 registered tests; separately pinned follow-ups resolve its fixture failure and execute the three skipped contracts. Real Win32 desktop and all 88 VS Code host groups pass. | The `8376252` debugger also passes six CTests and four real Win32 desktop sessions. Current-source SDK qualification remains open. Bare-host installation and Windows release qualification remain open. PostgreSQL requires an external runtime; the current UI provider is Win32. |
| macOS 15 Intel x86_64 | At `0c88d2f`, the relocated development SDK passes its installed standard-library campaign, twelve isolated PostgreSQL source/binary routes, installed debugger/checkpoints and corrected numeric-address validation. Earlier LLVM-ON/OFF, native assembly and embedded-context baselines also pass. | That SDK baseline has 567 passes, one failure and three skips. Newer source checks pass AppKit VM/native UI, DAP desktop, native lifetime, timestamp and ABI contracts on both Macs. Native incremental reuse passes at `860750e`; full editor acceptance, a current-source SDK and release qualification remain open. |
| macOS 15 Apple Silicon arm64 | At `3414ce5`, the relocated development SDK passes its installed standard-library campaign, twelve isolated PostgreSQL source/binary routes, installed debugger/checkpoints and corrected numeric-address validation. Native assembly and embedded context also pass. | That SDK baseline has 567 passes, one failure and three skips. Newer source checks pass AppKit VM/native UI, DAP desktop, native lifetime, timestamp and ABI contracts on both Macs. Native incremental reuse passes at `860750e`; full editor acceptance, a current-source SDK and release qualification remain open. |

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

- Native incremental reuse passes on both Macs at `860750e`. The compiler resolves
  loaded images to ordinary files and exact shared-cache backing files for full
  content hashing; its actual collector test passes on both Macs at `2632bc6`.
  The builder uses separate preparation and verification digest phases without
  caching source/manifest reads. Both actual linker-dependency probes collect
  8,321 inputs, and both real VM/native reuse and invalidation campaigns pass.
  A subsequent SDK archive-fallback refinement still needs hosted qualification.
  Incomplete or unsupported dependency profiles deliberately rebuild.
- A separate [development SDK workflow](../.github/workflows/macos-sdk.yml) now
  implements native Darwin consumer admission, root install layout, pinned
  documentation tooling, and staged dependency/relocation checks. Both
  candidates preserve all 260 payload files plus their manifests through relocation
  and pass the installed standard-library and debugger/checkpoint campaigns.
  The optional isolated PostgreSQL 16 profile now passes
  its coherent client/server and installed source/binary routes on both architectures;
  the scoped-address validation correction passes on both hosts. The corrected
  Intel retry at `0c88d2f` completes all installed campaigns, as recorded above.
- Both Mac hosts pass the managed and owned native lifetime contracts at
  `8376252`, including both owned memory modes. Each owned workload allocates
  and frees 36,000 guarded buffers; mandatory withheld-free controls reach the
  96 MiB retained-payload limit. The allocator controls also pass more than 2 GiB
  of cumulative allocation. This measures intercepted malloc-family payloads,
  not total address space, allocator metadata or mappings. The old Darwin
  virtual-address cap remains unusable and is not represented as passing.
- Desktop UI now uses one provider per platform: GTK on Linux and Win32 on
  Windows. The Windows provider passes the full desktop contract on a real
  interactive session -- provider contract, lifecycle release observation, the
  Matrix desktop suite and the interaction suite that drives real keyboard
  input -- while a session without an interactive window station still skips.
  On macOS the provider is AppKit. Its published contract passes against a
  real window server session on both hosted architectures; the newer `40017f1`
  provider passes 156 display and 41 in-process keyboard checks per host,
  and `nil.ui-display` now passes there too: the provider fixture, the
  lifecycle fixture and the Matrix desktop suite. Real keyboard input is
  covered in process by the AppKit provider fixture. At `5a98403`, both Macs
  pass all seven selected UI CTests and all ten desktop/interaction Matrix
  cases across interpreter/native routes. The test-only input driver uses
  each owning application's responders; its compiler adapter keeps injection
  out of host compilation. This does not qualify cross-process keyboard
  injection or accessibility. Earlier
  private GTK and AppKit prerequisite probes are retained as historical evidence,
  but GTK is no longer the macOS product provider. The LLVM-ON campaign at
  `965f159` passes all five selected CTests on both hosts, including all three
  Matrix desktop cases on each interpreter/native route. That earlier six-case
  baseline is superseded by the ten-case campaign above; editor acceptance
  remains separate.
- Editor desktop integration, untested provider configurations and release-package
  acceptance remain separate from the passing installed campaigns. Shipping a
  Mach-O tool alone does not qualify these surfaces.

The [macOS workflow](../.github/workflows/macos-bringup.yml) records the complete
inventory, including failures and skips. The [release gates](release/v1-profile.md#qualification-and-publication)
still apply before publishing a macOS SDK or declaring parity.

## New web-runtime APIs

The socket, secure-random and text-byte additions in the open web-runtime branch
are currently interpreter-backed. File timestamps share a stable native-metadata
implementation across interpreter and native routes: Linux text NIL, binary NIL,
JIT and AOT all pass unchanged-file and changed-metadata checks. Linux results
alone do not establish Windows or macOS support, and declarations do not establish
native/JIT execution. The actual native timestamp contract also passes on Windows
and both Mac architectures; full current-source packaging remains in progress.
Their evidence must join the older desktop, tooling and SDK baselines before
declaring current-source parity.

The private Windows LLVM-ON build at `965f159` passes all six focused web-runtime
contracts with no skips, including VM socket cleanup and the native timestamp
routes. At `19bd5f1`, the hosted campaign passes the same six interpreter contracts
on all four hosts, with exact inventories, no skips and independent raw-result
replay. The tested PR merge and head revisions are retained separately. Current
macOS editor coverage is still in progress. The socket/byte/entropy
native limitation is common to Linux, Windows and macOS; those APIs currently
require the interpreter on all three hosts.

## Current Windows source and editor follow-up

At `f79b94b`, the full private Windows build records 579 passes, one portable
path-fixture failure and three skips across 583 registrations. The corrected
fixture passes 71 MSVC checks separately. The formerly ELF-only symbol contract
now passes all sixteen generic ABI/IR checks against actual COFF objects.
Nathan's interactive session passes all ten Matrix VM/native desktop and input
cases, 137 provider checks, 235 lifecycle checks and window discovery. The retained
route reports contain 1,138 successes and 78 expected native rejections; the
six PostgreSQL routes and owned-cluster shutdown also pass.

The full real VS Code host passes 88 distinct groups with no exclusions, using
the `a518138` test-only physical-file alias correction with `f79b94b` products.
Extension unit coverage is 161 passes and one Windows-inapplicable POSIX FIFO
skip. An outer evidence wrapper incorrectly expected at least 100 host groups;
its raw failure is retained alongside independent replay of every artifact hash,
all six successful process phases and cleanup. This composed source evidence
does not replace the accepted SDK. The main-thread DAP execution change at
`8376252` subsequently passes all six debugger CTests, fourteen lifecycle sessions
and four actual desktop sessions on Windows and both Mac architectures. Linux
also passes all 586 source CTests at this revision. Details are in the
[web-runtime parity notebook](../notebook/2026-11-11-web-runtime-parity.md).
