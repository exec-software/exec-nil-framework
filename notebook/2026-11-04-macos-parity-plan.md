# macOS parity: initial audit and test environments

## Status

Initial source audit was at `3848e9c`. Same-source Windows/Linux development
SDK closeout is now recorded at `60e6bb9`; the first Intel and Apple Silicon
macOS workflow has been dispatched; the first native build exposed concrete
Apple SDK portability issues now being repaired. The user wants both
macOS architectures built on GitHub after Windows closeout. This entry tracks
bring-up evidence and remaining work, not a macOS support or release claim.

The Windows SDK at `5630388` has accepted full-suite, installed-consumer, GTK and
VS Code evidence, recorded in [the Windows notebook](2026-11-02-windows-port.md).
The matching Linux SDK at `5630388` now has full-suite and installed-consumer
acceptance, plus fresh paired debugger, Unicode, ordinary-I/O and same-process
checkpoint checks. Earlier `d5e8758` evidence remains historical.

## Question

How do we establish macOS parity with the Linux reference across both language
surfaces, each execution route, the STL, tools and editor without a local Mac?

## Evidence from the source

| Area | Observed implementation | Work or verification needed |
| --- | --- | --- |
| Host discovery | Compiler, interpreter and linker have `__APPLE__` executable-discovery branches. | Run installed and relocated SDKs with paths containing spaces and Unicode. |
| Native artifacts | `nillinker/main.cpp::validate_host_object` recognizes Mach-O relocatable objects; assembler tests recognize Mach-O. | Match object CPU and pointer width to the host; the Darwin branch currently checks magic and object type without the Linux branch's architecture checks. Prove split object/link/run on both architectures. |
| Executable context | `nilcompiler/src/llvm_backend.cpp` permits native embedding only in x86_64 Linux ELF and Windows PE/COFF; `executable_context.cpp` has no Mach-O inspector. | Implement and validate Mach-O context emission and bounded inspection, including retention through linking. This AI-facing feature is part of the target parity scope. |
| Incremental builds | Analysis collects loaded compiler modules on Linux and Windows in `nilcompiler/src/main.cpp`; there is no Darwin branch there. | Enumerate Darwin loaded-code dependencies and native link inputs, with regressions for changed dependencies. Treat incomplete discovery as non-cacheable. Absence of a branch is a source finding, not a reproduced stale-cache incident. |
| Runtime and IPC | `scripts/sdk_runtime.py` recognizes `libnil.dylib`; database loading has macOS library names; AI transport uses `getpeereid` on Apple hosts. | Exercise shared-runtime ABI, provider discovery, owner authentication, disconnects, debugger and same-process checkpoints on macOS. |
| UI | `nilcompiler/src/ui_runtime.cpp::Api` loads GTK on Linux and Windows only. | Add a macOS provider and run live lifecycle/input tests. Investigate GTK's macOS backend before selecting a separate Cocoa implementation. |
| Apple Silicon | LLVM initializes the host target, but native `INLINE_ASM` explicitly requires x86_64. | Prove ordinary arm64 code generation and ABI independently; design an arm64 assembly profile with equivalent behavioral tests. Existing x86 instructions must keep rejecting a mismatched target. |
| Distribution | `scripts/assemble-release.py` measures ELF dependencies; strict `sdk_qualification.py` release checks require Linux x86_64. | Add a separately reviewed Mach-O dependency/deployment-target policy, dylib relocation checks and distribution/signing workflow before release qualification. Keep development acceptance distinct. |
| Continuous testing | `.github/workflows/source-validation.yml` currently runs Linux framework and extension jobs. | Add explicit macOS host/architecture jobs and retain build/test artifacts. A green compile or a skipped route is not parity evidence. |

Additional early probes must cover POSIX process ownership and timeout cleanup,
JIT execution, dynamic libraries, case-insensitive filesystem collisions and
Unicode filename normalization. The existing tests' exact-name assumptions need
review against actual filesystem behavior. These are investigation targets,
not failures established by this source audit.

## Test environments

The GitHub API confirmed `exec-software/exec-nil-framework` is public and Actions
is enabled on November 4, 2026. GitHub currently supplies standard Intel and
Apple Silicon macOS runners; standard runners are free for public repositories.
Candidate explicit labels include `macos-15-intel` and `macos-15`. Select and
record the actual OS image, Xcode, compiler and LLVM versions when constructing
the first job; a label alone does not freeze its image contents.
See [GitHub's runner documentation](https://docs.github.com/en/actions/how-tos/write-workflows/choose-where-workflows-run/choose-the-runner-for-a-job).

Recommendation: begin with bounded hosted build/test jobs on both architectures.
Intel helps isolate OS-porting issues from the arm64 transition; Apple Silicon
gets its own native results. Introduce jobs manually first so build failures and
prerequisite gaps can be inspected before making the matrix a required check.
Keep failures visible and record exclusions explicitly during bring-up.

A persistent remote Mac would support iterative debugging and desktop acceptance.
Apple documents macOS VMs hosted on Apple silicon Macs, so that route still needs
access to Apple hardware, locally or remotely. See
[Apple's virtualization documentation](https://developer.apple.com/documentation/virtualization/virtualize-macos-on-a-mac).
EC2 Mac is one rental option, with a minimum Dedicated Host allocation of
24 hours; obtain a concrete cost and host proposal before provisioning it.
See [AWS's Mac instance documentation](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/ec2-mac-instances.html).

The [OSX-KVM project](https://github.com/kholia/OSX-KVM) documents a community
QEMU/KVM route on Linux. Its README also identifies missing graphics acceleration
and other device limitations in the base setup. This could be a separate
compatibility experiment after Windows closeout. It would require choosing the
guest version, checking host prerequisites and reviewing the proposed VM setup
before installation. Successful x86 VM tests would establish only that guest's
behavior; native Apple Silicon and desktop-provider acceptance remain separate.

No macOS VM or persistent cloud host was provisioned. The initial audit did not
create CI jobs; the subsequent manual workflow is described below.

## Decision and follow-up

1. Finish Windows closeout against a named Linux baseline. Compare capabilities
   and installed routes, then run any missing current-revision acceptance,
   including a refreshed Linux SDK and corresponding installed runtime/debugger
   checks. Preserve earlier results under their actual source revisions.
2. Track shared limitations separately: external-state rollback, code hot reload
   and native `environment.list` are shared implementation gaps. Resolving them
   is language work, not evidence that the Windows port is lagging Linux.
3. Establish macOS Intel and arm64 build baselines, with LLVM enabled and
   disabled. Record every registration, failure, skip and missing prerequisite.
   First success means a build plus non-vacuous frontend/interpreter tests,
   without claiming native or installed SDK acceptance prematurely.
4. Address the observed Mach-O, dependency-tracking, UI and architecture gaps.
   Reuse the Linux behavioral contract for both Ny and Matrix. Cover textual and
   binary NIL, bundles, JIT, object/link/run and AOT, then the complete STL.
5. Produce separate architecture SDKs with root tools, `nillib`, `nilstd` and
   docs. Test fresh extraction and relocation, dependency discovery, shared
   runtime, debugger, AI checkpoints, GTK and VS Code against those exact bytes.
   Record native signing/JIT requirements as observed rather than disabling
   host protections to make a test pass.
6. Declare parity only against an explicit supported-feature matrix with real
   route results. Retain architecture-specific assembly contracts. Release
   promotion requires the reviewed macOS packaging and host policy afterward.

The immediate implementation task after Windows closeout is the first bounded
macOS build matrix and retained failure inventory. Buying hardware is not a
prerequisite for that step.

## Windows closeout and macOS build workflow

The same-source Windows/Linux SDK and installed-contract comparison is now
recorded in [the Windows notebook](2026-11-02-windows-port.md). The refreshed
Linux SDK at `5630388` passed all 568 CTests without skips and 1,184 installed
consumer executions. Windows passed 570 tests with its service-display skip
supplemented by separate installed GTK acceptance. Both hosts passed fresh
installed debugger, Unicode, ordinary-I/O and same-process checkpoint gates.
Checkpoint `60e6bb9` records this evidence. The final audit also identified a
small experimental Windows `nilagent` title-filter discrepancy for focused repair.

The new [macOS bring-up workflow](../.github/workflows/macos-bringup.yml) is
manually dispatched. It contains four native jobs: `macos-15-intel` / x86_64 and
`macos-15` / arm64, each with LLVM ON and OFF. Jobs run at most two at a time,
build all framework tools, retain the complete registered CTest inventory even
after a partial build, and preserve raw diagnostics, JUnit, image/toolchain/source
identities and build products. The binary archive retains executable modes.
Failures remain failures; this workflow does not publish or qualify an SDK.

Both selected runner images document preinstalled LLVM 18.1.8, CMake, Ninja and
Python. The job checks the actual architecture and LLVM major version, records
the image and Xcode versions, and uses Apple SDK SQLite headers. A coherent
existing PostgreSQL 16 distribution is discovered from its own `pg_config`, or
the provider is explicitly unavailable and its tests retain their actual results.
No Homebrew upgrade or source dependency build is started: current formula
metadata lacks Intel macOS bottles for several relevant dependencies. GTK's
missing macOS backend and any missing documentation tool are recorded explicitly.
The primary image and formula references are
[runner images](https://github.com/actions/runner-images/tree/main/images/macos)
and Homebrew metadata for [LLVM 18](https://formulae.brew.sh/api/formula/llvm@18.json),
[PostgreSQL 16](https://formulae.brew.sh/api/formula/postgresql@16.json) and
[SQLite](https://formulae.brew.sh/api/formula/sqlite.json).

The [evidence collector](../scripts/ci/macos-evidence.py) distinguishes failures,
skips, missing and unexpected registrations, duplicate names and unknown status.
Its ten [inventory controls](../scripts/ci/test_macos_evidence.py) run before
configuration. A partial or absent JUnit file cannot establish successful
coverage. Actual macOS execution results must be recorded after dispatch;
host-side workflow checks alone establish no macOS execution support.

## IA-32 target decision

The user clarified that “x86 assembly” meant 32-bit IA-32, distinct from the
existing x86_64 profile. The recommendation for this work is to preserve
x86_64, bring up native arm64, and defer IA-32 until there is a concrete consumer
and target environment. The current qualified SDK profile is x86_64; no
qualified IA-32 SDK is being removed.

IA-32 support would require an explicit native target, a compatible runtime and
calling convention, pointer/layout and linker/object validation, SDK packaging,
and route/ABI tests. Adding an assembly spelling alone would not establish that
compatibility. Future IA-32 work should keep its target declaration explicit and
provide focused regressions for the actual legacy consumer. Existing x86_64
assembly must continue rejecting mismatched native targets; arm64 needs its own
instruction and operand contract.

## First native GitHub execution and immediate repairs

Workflow commit `89f7e62` was pushed and dispatched as
[run 41930293586](https://github.com/exec-software/exec-nil-framework/actions/runs/41930293586).
The Windows title-filter follow-up was subsequently committed as `0556fce`, with
34 desktop assertions and the concrete noninteractive prerequisite check recorded
in the Windows notebook. The first macOS run remains bound to `89f7e62`.

Both architectures passed exact source/architecture checks, the initial nine
collector controls, provider observation and native CMake configuration. The
first completed arm64/LLVM-ON job used macOS 15.8.2, image `20261028.0489.1`,
Xcode 16.4 / SDK 15.5, and LLVM 18.1.8. Its complete build failed at two actual
source portability points:

- This Apple libc++ has no floating-point `std::from_chars` overload. The common
  numeric-literal header therefore prevented the compiler and dependent tools
  from completing.
- Darwin exposes `sigemptyset` as a macro; three globally qualified calls in the
  process-runner regression fixture failed to compile. Removing only the `::`
  prefixes leaves the signal behavior intact. The Linux process-runner
  regressions passed after that adjustment.

The original arm64 record has 567 registrations and observations: 32 passed,
488 failed and 47 skipped. Many failures and skips follow from absent compiler
products; these counts do not establish 488 independent language defects. Raw
logs, JUnit, source/image identity and partial build products are retained under
`/home/nathan/vm/nil-win11/artifacts/macos-bringup-20261104/run-41930293586/`.

CTest 4 also emits the valid JUnit status `fail`, which the initial collector
flagged as unknown despite classifying the failure child correctly. The
collector now recognizes that status as a failure even without a diagnostic
child. All ten controls pass, and replay of the original arm64 JUnit retains
exactly the same result counts with no inventory errors. The original artifact
is preserved; the corrected replay is a separate diagnostic sidecar.

Read-only triage of that partial-build inventory found thirteen independent
failing CTests across six areas. The other 475 failures depend on missing build
products; 42 of the 47 skips follow missing executables or fixture dependencies.
The five explicit gates concern PostgreSQL, GTK and host-specific lifetime
checks. These are initial observations, not completed macOS qualification:

| Observed area | Follow-up and scope |
| --- | --- |
| Unicode filenames | Fixtures assume NFC and NFD names can coexist distinctly; this filesystem aliases them. Correct the host/fixture contract before inferring an encoding defect. See `tests/tools/byte_file_unicode_paths_contract.py`, `ordinary_io_parity_contract.py` and `tests/utf8_file_paths_tests.cpp`. |
| Temporary path spelling | `/var` versus canonical `/private/var` breaks ordinary-I/O, project discovery and pipeline-artifact expectations. Resolve test path identity explicitly. |
| Process identity | `tests/tools/process_contract_support.py` assumes `/proc/PID/stat` on every POSIX host. Add a Darwin identity implementation while preserving stale-PID checks. |
| Owned-process cleanup | `validation/ny-3d/scripts/debugger_process.py` receives `EPERM` from `killpg`, including after child completion. Obtain a focused Darwin probe before changing ownership or cleanup guarantees. |
| Concurrent SDK publication | The reader in `tests/publish_sdk_concurrency.py` observed `EINVAL` through a concurrently replaced SDK symlink. Underlying cause remains unresolved. |
| Native linker arguments | Darwin arm64 linking succeeded but warned that `-no_pie` is deprecated/ignored; strict stderr acceptance stopped before execution. Review the architecture-specific linker policy. |

These follow-ups are separate from the established Mach-O context, ARM assembly,
UI provider, installed SDK and distribution gaps in the original audit.

The Intel/LLVM-ON job completed with the same two compiler-error classes and
the same 567-case counts. It used macOS 15.7.9, image `20260824.0482.1` and
Xcode 16.4. Architecture and image identities remain separately recorded.

The Apple numeric-parser compatibility path uses `strtod_l` with an immutable
private C locale after the existing strict decimal grammar checks. A scoped
floating-point environment enforces nearest-even conversion and restores caller
rounding, exception flags and errno. Finite nonzero subnormals remain accepted
even when the C library sets `ERANGE`; overflow and underflow to zero are still
rejected. Exact zero retains its original exponent-independent fast path.
Linux and Windows continue using `std::from_chars`.

Focused host probes passed 190 controls on the existing Linux path and 282 on a
private copy of the Apple algorithm adapted only for the host locale header and
branch selection. Those checks cover exact bits at halfway and range boundaries,
subnormals, signed zero, four rounding modes and caller-state restoration. They
are algorithm controls, not Apple API qualification. The next native jobs must
exercise the actual Apple branch, including the decimal-comma thread-locale
control in the parser suite.

Final probe evidence is in
`/home/nathan/vm/nil-win11/artifacts/macos-bringup-20261104/numeric-fallback-probe-v2/report.json`,
including exact source hashes and strict C++17 build commands. API contracts
were checked against Apple's [`strtod_l`](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/strtod_l.3.html)
and [`newlocale`](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/newlocale.3.html)
documentation. The scope includes preserving output and caller state on rejected
conversions as well as successful ones.

All four first-run jobs completed and retained artifacts. Both LLVM-OFF jobs
reported 563 registrations/observations: 32 passed, 483 failed and 48 skipped,
with the same two build-error classes as LLVM ON. All four source identities
remained unchanged. `first-run-summary.json` records the original inventories'
hashes; no failed initial result has been overwritten or relabeled. The immediate
source and collector fixes passed independent review before the next dispatch.
