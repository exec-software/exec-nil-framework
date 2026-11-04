# macOS parity: initial audit and test environments

## Status

Initial source audit was at `3848e9c`. Same-source Windows/Linux development
SDK closeout is now recorded at `60e6bb9`; the first Intel and Apple Silicon
macOS workflow is prepared, with native execution pending. The user wants both
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
Its nine [inventory controls](../scripts/ci/test_macos_evidence.py) run before
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
