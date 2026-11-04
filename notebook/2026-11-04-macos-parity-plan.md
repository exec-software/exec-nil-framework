# macOS parity: initial audit and test environments

## Status

Observed source audit at `3848e9c`; implementation and macOS execution are pending.
The user wants Windows brought into a good state and pushed before macOS joins
Linux as a fully tested development platform. They do not own a Mac and are
interested in a macOS VM experiment. This entry records a starting plan, not a
macOS support or release claim.

The Windows SDK at `5630388` has accepted full-suite, installed-consumer, GTK and
VS Code evidence, recorded in [the Windows notebook](2026-11-02-windows-port.md).
Its documentation checkpoint `3848e9c` is pushed. The latest installed Linux SDK
acceptance still belongs to `d5e8758`; newer Linux source tests do not replace a
same-revision installed SDK comparison.

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

No macOS VM, cloud host or new CI workflow was provisioned by this audit.

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
