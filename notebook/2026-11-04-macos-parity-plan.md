# macOS parity: native build baseline and remaining work

## Status

The Windows/Linux development SDK closeout is recorded at `60e6bb9`. Native
Intel and Apple Silicon macOS builds now pass on GitHub at `c123849`, with LLVM
both enabled and disabled. All four backend-selection checks pass, and actual
compiler, interpreter, Matrix and checkpoint execution is established. Native
execution also passes targeted route tests on both architectures. The full
CTest inventory still exposes macOS parity gaps; build support is established,
but macOS SDK packaging, installed acceptance and release qualification remain
open. The initial source audit below was at `3848e9c`; subsequent sections retain
each run's source identity, failures and repairs.

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

## Original plan and current follow-up

Steps 1 and 3 are complete: paired Windows/Linux development acceptance and
four native macOS build baselines are recorded below. Steps 2 and 4–6 retain
their feature, installed-package and release scope.

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

The first bounded macOS build matrix and retained failure inventory now exist.
Buying hardware was not a prerequisite for this build milestone; live desktop
acceptance still needs a suitable environment.

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
`macos-15` / arm64, each with LLVM ON and OFF. The initial runs used two concurrent jobs; after setup was verified, the
bounded four-job matrix was enabled together to shorten native iteration. Jobs
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

## Cross-platform rejected-conversion output

A bounded Windows probe of the exact `c4a9f02` header and committed numeric test
function passed all numeric-bit, grammar, errno and rounding checks, but exposed
twenty unchanged-output failures: MSVC's conversion wrote a value for each of
five out-of-range inputs under all four rounding modes. The original failure
report remains in `windows-numeric-c4a9f02/evidence/` under the macOS evidence root.

The non-Apple path now converts into a local `double` and assigns the caller's
output only after every success check. No test was relaxed, and the Apple path
was unchanged. The same 190 controls passed on Linux and in a fresh strict
MSVC `/W4 /WX /O2` Windows run. The guest checkout and accepted SDK were unchanged,
and owned cleanup was verified. Final Windows report:
`windows-numeric-output-normalization/evidence/report.json`, SHA-256
`cea23b8ebfcb29c5e7779fa9edfd7479fced8452ea9514534648b0eef4c96fa0`.
The independently replayed source/binary/log record is
`windows-numeric-output-normalization/reconciliation.json`, SHA-256
`441a3c609e0eb25976f187ed37fbbbfce80faab98c18e5335857571e454c1fd4`.

The second native macOS matrix is
[run 41932302013](https://github.com/exec-software/exec-nil-framework/actions/runs/41932302013)
at `c4a9f02`; its source identity is preserved separately from this subsequent
non-Apple-only adjustment.

## Second native run: checkpoint template linkage

The corrected arm64/LLVM-ON run compiled the source translation units. The
actual Apple parser suite passed, including the private-locale, subnormal and
floating-environment controls; `nillinker.process-deadline` also passed. The
collector reported 568 registrations/observations with no inventory errors:
67 passed, 475 failed and 26 skipped.

Linking the interpreter and its consumers then exposed undefined
`CheckpointCloner::typed<T>` specializations. Clang diagnosed the later-defined
private member template as used but undefined through earlier `std::visit`
callers. A private extraction of the exact class reproduced the issue with
local Clang 18 and libstdc++, so it is not specific to Apple's libc++.

The fix moves the complete `typed()` definition ahead of its callers. Independent
review confirmed its 2,222-byte body is identical, private access is retained,
and class data layout, accounting, depth/alias/cycle behavior and unsupported
value rejection are unchanged. The same Clang probe now compiles and runs,
cloning seven supported variant alternatives and rejecting boxed/foreign values.
Evidence is in `checkpoint-clang-probe/report.json` under the macOS evidence root.
The complete current `vm.cpp` and checkpoint fixture also compiled and linked
with Clang 18, then passed all 210 checkpoint checks with empty stderr. Existing
Linux core/runtime archives were read only and their hashes remained unchanged;
the full-VM report is `checkpoint-clang-probe/full-vm/report.json`. Native macOS
link/execution verification remains for the next matrix.

## Third native run: executable tools and missing output identity

[Run 41933429641](https://github.com/exec-software/exec-nil-framework/actions/runs/41933429641)
at `2a6adcc` built every arm64/LLVM-ON framework target successfully. Actual
Apple parser/locale controls, VM checkpoint controls, source compiler smoke,
textual and binary interpreter smoke and Matrix execution passed. The retained
568-case inventory has 259 passes, 304 failures and five explicit skips, with
no evidence errors. This establishes native tool builds and real language
execution, while the remaining failures still prevent macOS parity.

The LLVM capability probe reached the assembler and failed on an absent output
file: Apple's `std::filesystem::equivalent` reported `operation_not_supported`
instead of the missing-file error handled by `same_path`. An explicit observed
`not_found` check is needed while preserving equivalent-file checks and errors
for existing, inaccessible or unknown identities. A blanket suppression of
`operation_not_supported` would weaken the existing input/output alias contract.

The older second matrix was cancelled once the checkpoint link failure had
been fixed and its completed arm64 evidence preserved. Intel-ON's retained
diagnostic inventory also has 568 cases (67 pass, 475 fail, 26 skip); Intel-OFF
was incomplete and arm64-OFF never ran. Cancelled profiles are not acceptance
results. Available logs and partial artifacts remain under `run-41932302013/`.

With runner setup established, the four native profiles now run concurrently
(up from two), keeping the same fixed matrix, job deadlines and full test
inventory. This reduces iteration latency without excluding a route or test.

The assembler fix now probes both paths and accepts absence only when status is
explicitly `not_found` with no error or `no_such_file_or_directory`. Other lookup
errors remain fatal, existing pairs still use filesystem equivalence, and the
pre-lowering/pre-publication identity checks plus no-follow atomic publication
remain intact. A private Clang assembler build passed the full CLI contract and
the actual native-object capability probe. Added controls preserve an existing
output on missing input and reject a dangling output symlink without creating
its target. Evidence: `assembler-identity-probe/report.json`.

Darwin's obsolete `-Wl,-no_pie` argument was also removed; Linux `-no-pie` remains.
LLVM 18's [x86 target policy](https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/lib/Target/X86/X86TargetMachine.cpp#L159-L192)
and [ARM target policy](https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/lib/Target/AArch64/AArch64TargetMachine.cpp#L267-L277)
select PIC for these Darwin targets, and [Apple's linker](https://github.com/apple-oss-distributions/ld64/blob/main/src/ld/Options.cpp#L5507-L5535)
defaults to PIE for the supported hosts. Both previous native link tests had
succeeded but emitted the retained deprecation warnings; arm64 explicitly
ignored the flag. Independent review and the unchanged Linux host-object and
process-deadline tests passed (2/2). Evidence and primary-source response hashes
are in `linker-pie-focused/`. Actual native macOS verification follows in the
next matrix; no test diagnostic or assertion was weakened.

A bounded private Windows assembler rebuild against unchanged reference build
libraries also passed all four targeted cases: fresh x64 COFF output, same-file
rejection, hardlink rejection and absent-input output preservation. The copied
`main.cpp` exactly matched `0b2b884`; 75 reused library/object inputs, headers,
checkout and accepted SDK hashes were unchanged. No staging or process orphans
remained. Evidence: `windows-assembler-path-status/reconciliation.json`, SHA-256
`220b1438ec879b778c19164e55074fd439825dcb925ec8a82e241e5cd36f6755`.
This is focused cross-platform tool verification, not a replacement Windows SDK.

The fourth matrix at `0b2b884` is
[run 41935062099](https://github.com/exec-software/exec-nil-framework/actions/runs/41935062099).
The superseded third run's completed arm64 results were preserved before
cancelling remaining jobs to advance the repaired source. Its available Intel
logs and partial artifacts are retained separately; cancelled profiles are not
acceptance results.

## Fourth matrix: build baseline and remaining test contracts

All four `0b2b884` profiles passed the full build and backend-selection checks.
LLVM-ON selection executes real host-object emission; LLVM OFF must explicitly
report the unavailable backend. The first completed profile, arm64/LLVM OFF,
recorded 564 registrations and observations: 391 passes, 27 failures and 146
skips, with unchanged source and zero collector errors. Skips include disabled
native routes and provider/platform gates; they are not passing feature coverage.
Compiler smoke, textual/binary interpreter smoke, Matrix execution, parser
controls, VM/AI checkpoints and host-object linking passed.

The 27 failures in that LLVM-OFF profile were read-only triaged as follows:

| Count | First observed failure boundary |
| --- | --- |
| 14 | Temporary-path or metadata assertions, including project/pipeline, switch, niltest, analysis/editor metadata, Matrix function values, reflection/query and DAP. `/var` versus `/private/var` is explicit in several records; the similarly shaped remaining assertions still need focused confirmation. Their downstream coverage is not established. |
| 3 | Unicode namespace fixtures assuming distinct NFC/NFD filenames. |
| 1 | Ordinary-I/O oracle with mixed namespace, path and ownership assumptions. |
| 6 | Route/harness cleanup reports `EPERM`, including byte/I-O, debugger, UI-harness and PostgreSQL-fixture controls. |
| 2 | Process-identity controls assume `/proc/PID/stat`. |
| 1 | Concurrent SDK publication reader reports `EINVAL`. |

No additional compiler/build/backend blocker was identified from those 27
failures. They remain failed tests pending fixes and new native evidence.

Arm64/LLVM ON completed with 335 passes, 228 failures and five skips across all
568 registrations; source was unchanged and the collector found no evidence
errors. All 27 LLVM-OFF failures recur. The additional native failures are
dominated by successful AOT links rejected for unexpected stderr: the compiler's
direct executable route and an assembler test helper still supplied Darwin
`-no_pie`, independently of the corrected standalone linker. Both remaining
branches are now removed, Linux flags and output assertions remain unchanged,
and the compiler comment now distinguishes Linux non-PIC from Darwin PIC.
Complete compiler-source syntax, Python syntax and independent review passed;
evidence is in `darwin-pie-followup/`. No active `no_pie` use remains in code.

Other distinct LLVM-ON observations include the known ARM assembly profile gap
and an AI checkpoint TCP `sendall` timeout at
`tests/interpreter/ai_checkpoint_contract.py:194`. The same AI checkpoint
contract passed in the LLVM-OFF profile. This remains timing/transport evidence
requiring follow-up, not a compiler build failure or a silently waived test.

Intel/LLVM OFF also completed with the same 391-pass, 27-failure, 146-skip counts
and passing compiler/interpreter/Matrix/parser/checkpoint controls. Its payload
inventory is recorded separately under the fourth-run evidence root.

All fourth-run profiles completed at exact `0b2b884`, with every build/backend
selection gate passing and every CTest registration observed:

| Architecture | LLVM | Passed | Failed | Skipped | Registered/observed |
| --- | --- | ---: | ---: | ---: | ---: |
| arm64 | ON | 335 | 228 | 5 | 568 |
| arm64 | OFF | 391 | 27 | 146 | 564 |
| x86_64 | ON | 335 | 228 | 5 | 568 |
| x86_64 | OFF | 391 | 27 | 146 | 564 |

The failed-test sets need not be identical despite matching totals. The
large-frame AI TCP timeout occurred only in arm64/LLVM ON: the request contains
a 62,000-character unknown field, and sending it timed out before checkpoint
operations. Intel LLVM ON and both LLVM-OFF profiles passed that IPC contract.
VM checkpoint-state tests passed throughout.

A separate read-only streamed inspection verified 44/44 tool payloads: all
eleven expected tools per profile are regular owner-executable Mach-O64
`MH_EXECUTE` files with the correct CPU type and exact collector byte counts
and SHA-256 values. Source/metadata/archive inputs remained unchanged. This
checks build payloads, not SDK packaging or full platform qualification. Report:
`run-41935062099/artifact-tool-inventory.json`, SHA-256
`a97f160c512c6b4290c71bb56a13ff053616ee0c12205f1ec3cecd21f4c7e5d7`.

The final direct-compiler/test-helper PIE cleanup was committed as `c123849`
and dispatched as
[run 41937145303](https://github.com/exec-software/exec-nil-framework/actions/runs/41937145303).
The complete fourth-run evidence remains separate and unchanged.

## Final build baseline at `c123849`

[Run 41937145303](https://github.com/exec-software/exec-nil-framework/actions/runs/41937145303)
completed all four profiles at exact source
`c1238494b8a4c2869acb73e629cdd8a14f8ae6a5`. Every full build and backend-selection
check passed. Each complete CTest inventory has unchanged source, zero collector
errors, and exactly one observation for every registration:

| Architecture | LLVM | Passed | Failed | Skipped | Registered/observed |
| --- | --- | ---: | ---: | ---: | ---: |
| arm64 | ON | 533 | 30 | 5 | 568 |
| arm64 | OFF | 390 | 28 | 146 | 564 |
| x86_64 | ON | 534 | 29 | 5 | 568 |
| x86_64 | OFF | 391 | 27 | 146 | 564 |

Compiler smoke, textual/binary interpreter smoke, Matrix execution, parser
controls, VM checkpoints and the AI checkpoint IPC contract passed in all four
profiles. Both LLVM-ON profiles passed the assembler CLI and representative
full JIT/AOT route tests, including native string escaping and lexical/global
shadowing. The deprecated Darwin linker diagnostics no longer appear. This is
native language execution evidence on both architectures, beyond tool builds.
LLVM-OFF results do not establish execution of disabled native routes.

The original 27 failed contracts recur in all four profiles. The additional
LLVM-ON failures are now specific:

- Both architectures fail `nilbuild.incremental-contract`: NIL output is reused,
  but the native output rebuilds. Darwin native dependency discovery remains
  incomplete, so native reuse is not established.
- Both fail `ny.tools.context-manifest` during Mach-O context inspection. The
  harness cannot decode compiler stderr as UTF-8 (raw byte `0xcf` at offset 62),
  consistent with the unsupported Mach-O dispatch falling into text parsing.
  Fix bounded format detection and diagnostics as well as Mach-O context
  embedding/inspection; changing the harness decoder alone would not deliver
  the missing feature.
- Only arm64 fails `matrix.routes.inline-assembly-native`, matching the absent
  ARM assembly profile. Intel passes its existing x86_64 native assembly route.

Arm64/LLVM OFF additionally failed `nil.project-edition`: the first manifest run
produced no captured output instead of `REACHED-END`. Its failure message omits
the captured process result, so this record does not distinguish a launch or
process failure from program behavior. The same contract passed in the previous
arm64/OFF run and the other three final profiles. Preserve this unresolved
observation and improve the diagnostic before assigning a cause; no retry or
waiver was used to change this run's counts.

The five LLVM-ON skips concern PostgreSQL availability, the absent macOS UI
provider/display, and two host-specific lifetime gates. The 146 LLVM-OFF skips
also include disabled native routes. The earlier arm64 AI large-frame send
timeout remains in the fourth-run evidence even though the final IPC contract
passed everywhere; this single clean run does not prove that timing issue fixed.

All four final artifacts and job logs are retained under
`/home/nathan/vm/nil-win11/artifacts/macos-bringup-20261104/run-41937145303/`.
`full-four-profile-summary.json` binds the counts, failed/skipped names,
representative execution controls, source and inventory hashes to the completed
GitHub run. A separate streamed inspection passed for all 44 tool payloads:
eleven regular owner-executable Mach-O64 `MH_EXECUTE` files per profile, with
the expected CPU, byte counts and SHA-256 values. Inspection inputs remained
unchanged. The report is `artifact-tool-inventory.json`, SHA-256
`9607f760e5c7feb57af9702fd7ae05431444525772d4c37b5512293bab3ae42f`.
These archives contain build products; they are not installed or release-qualified
SDK packages.

The same source also passed all three
[Linux source-validation jobs](https://github.com/exec-software/exec-nil-framework/actions/runs/41937098841):
framework LLVM ON, framework LLVM OFF, and the VS Code extension. The accepted
Windows/Linux SDK bytes remain at `5630388`; the subsequent portability fixes
and their focused Windows/Linux checks do not replace those packages.

The next parity pass should first resolve the shared macOS path, namespace and
owned-process assumptions without weakening cleanup or identity checks. That
will expose the downstream debugger/editor/project behavior currently blocked
by early assertions. Then complete Mach-O context handling and native dependency
tracking, add the arm64 assembly contract, and qualify providers and installed
SDKs. UI, dylib relocation/deployment, editor desktop acceptance and release
qualification remain explicit follow-up work. No Lich release or Anomalous SDK
replacement was performed during this build milestone.

## Documentation and filesystem portability follow-up

The documentation checkpoint `f2f5244` introduces a shared
[platform support matrix](../docs/platform-support.md) and updates sixteen
Markdown files. Outdated Windows-only AI-control/unfinished Unix transport
claims now describe implemented local control and the restricted same-process
checkpoint profile. README, getting-started, STL, tooling, UI and design
summaries separate actual macOS builds/execution from incomplete installed and
release qualification. Historical Matrix samples no longer claim the frontend
does not exist. Validation resolved 257 local links and built all 67 HTML pages;
the report is `docs-refresh-20261104/validation.json` under the original macOS
evidence root. The recorded acceptance source revisions remain explicit.

Filesystem fixture checkpoint `99a8ee8` repairs several host assumptions while
preserving the expected language behavior:

- Fourteen compiler/editor/debugger/project fixtures now canonicalize only their
  newly created temporary root. Exact output-identity assertions and intentional
  alias cases are unchanged. Running those fourteen checks on Linux with a
  symlinked `TMPDIR` reproduced fourteen failures before the change and fourteen
  passes afterward (parallel 2, 11.95 seconds). This reproduces the `/var` versus
  `/private/var` mismatch independently; native macOS verification still follows.
- Unicode filename cases use distinct ASCII identifiers or parent directories
  so NFC/NFD payloads cannot overwrite each other through APFS lookup identity.
  Ordinary-I/O fixtures observe actual same-file aliases before seeding
  wrong-spelling sentinels and record those observations. The oracle continues
  comparing exact stored names, every file's bytes, protected sentinels and
  unexpected entries. New negative controls deliberately create a normalized
  output spelling and require failure; an independent host-identity control
  proves an equivalent alias cannot overwrite its intended seed.
- The C++ malformed-path fixture observes whether the filesystem accepts raw
  invalid-UTF-8 names. Only host `EILSEQ`/`EINVAL` creation rejection is admitted;
  unrelated host errors still fail. All sixteen runtime malformed-path cases
  still require the exact UTF-8 validation diagnostic and unchanged namespace.
  The test reports its actual raw-name sentinel count. Linux passed eight valid
  cases, sixteen malformed cases and eight raw-name sentinels. This models
  [APFS's documented valid-UTF-8 creation rule](https://developer.apple.com/library/archive/documentation/FileManagement/Conceptual/APFS_Guide/FAQ/FAQ.html)
  without normalizing runtime path inputs or fabricating on-disk names.
- The concurrent SDK-publication fixture also canonicalizes its private roots;
  its exact generation comparison failed before and passed after under the
  same Linux alias experiment. Its original Darwin reader `EINVAL` is not
  claimed fixed: future failures now retain the operation's full traceback.
  The isolated edition-run failure likewise now includes the process result
  in its diagnostic, without changing the expected program behavior.

All seven focused Linux Unicode/ordinary-I/O CTests passed, including interpreted
and native consumers and the independent oracle controls. The updated C++
raw-name test passed separately after its final adjustment, and the edition
contract passed. Failure reproductions and passing reports are retained under
`/home/nathan/vm/nil-win11/artifacts/macos-parity-20261105/` in `canonical-paths/`,
`namespace/` and `publication/` (the evidence directory uses UTC date naming).
These changes await native macOS execution; existing `c123849` counts remain the
current published baseline until that run completes.

## Darwin process harness and object identity follow-up

The owned-process harness now handles Darwin's zombie-only process groups.
[XNU's signal implementation](https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.140.69/bsd/kern/kern_sig.c#L1668-L1713)
can return `EPERM` when no member remains signalable. The harness accepts that
specific result only after its retained, unreaped child is known exited and two
bounded, complete group observations establish no live members. The second
observation must retain the leader and may only lose already-dead descendants;
new members, live states, incomplete observations and other permission failures
remain errors. The leader pins the group identity until verification finishes.

The separate process-contract fixture now observes Darwin creation identities
with `libproc`, and cleanup passes the recorded PID version to an identity-bound
kernel signal operation. It never substitutes a fresh process version or falls
back to a check followed by an unqualified PID signal. Native controls include
rejecting an intentionally stale version for the fixture's own sleeper before
valid cleanup. These private test-harness APIs are explicitly checked for
availability and layout; the planned native evidence boundary is the current
macOS 15 runners, not an inferred minimum OS version for the language SDK.
Primary source: [the libproc declaration](https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.140.69/libsyscall/wrappers/libproc/libproc.h#L132)
and [the retained-process signal implementation](https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.140.69/bsd/kern/proc_info.c#L3561-L3625).

Independent source review passed. Seven focused Linux CTests passed in 11.30
seconds, with source hashes unchanged across execution. The report is
`darwin-owned-process/linux-focused-results.json` under the original macOS
evidence root, SHA-256
`85033a03809924d9807c700ac261588bf5eeb3fe9fe46f1ccc1bc9f71b2a2d4e`.
Darwin API/layout and actual process behavior still require the hosted run;
host-side synthetic controls do not establish native success.

The new synthetic Darwin tests also run on Windows, whose Python need not
define `SIGKILL`. A test-only follow-up supplies Darwin's signal value within
the simulated fixture and restores the host module afterward. Removing
`SIGKILL` deliberately reproduced seven errors among fourteen synthetic tests;
all fourteen passed after the correction. Private Windows copies then passed
38 tests with two explicit platform-specific skips, with owned cleanup and
nine input hashes verified. Guest checkout and SDK were unchanged. Reconciled
evidence: `darwin-owned-process/windows-synthetic-followup/windows-reconciliation.json`,
SHA-256 `ab2a726a1da5a913522e949fc386a2de8bfed0d0dc9384e184be9f1ccac18a99`.

The standalone linker also now validates the complete Mach-O header, host CPU,
pointer width and byte order before invoking its driver. Its existing native
CLI test mutates a real object into five incompatible/truncated variants and
requires the exact rejection while preserving output and leaving no staging.
The test's deliberately hanging driver ensures an unrelated driver error cannot
substitute for pre-driver validation. An exact extracted validator reproduced
the previous wrong-CPU acceptance and passed eight controls each with real
cross-emitted x86_64 and arm64 Mach-O objects. This executes the selected header
logic on Linux, not macOS. Evidence is in `macho-linker/` under the newer
`macos-parity-20261105` root. CPU constants and header sizes match Apple's
[Mach-O declarations](https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.140.69/EXTERNAL_HEADERS/mach-o/loader.h)
and [machine types](https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.140.69/osfmk/mach/machine.h).
Independent review passed; Linux and Windows object-validation branches are
unchanged, and native macOS verification remains pending.

## Mach-O executable context implementation

The context reader and LLVM emitter now support thin little-endian Mach-O64
on x86_64 and arm64, for relocatable objects and linked executables. The
`ny.context.v2` payload and its disclosure limits are unchanged. Mach-O stores
it in `__DATA,__nyctx` as regular non-instruction data, retained through
`llvm.used` and the section's no-dead-strip attribute. This is inert metadata;
the surrounding linked segment is not claimed to be memory-read-only.

Inspection bounds the header, command and section tables, segment/file ranges,
relocations and known link-edit metadata ranges before decoding the context.
It rejects duplicate context sections, metadata/section overlaps, relocatable
context payloads, unsupported image kinds and encryption commands. Universal/fat,
32-bit and swapped-endian formats remain outside this profile. Native magic is
dispatched before textual NIL; unsupported binary inputs produce bounded
diagnostics. Previously admitted UTF-8 annotation text, including non-NUL
controls, remains accepted and has a regression control.

Independent review identified and closed additional metadata-range checks and
the text-control compatibility case before freezing the implementation. Final
Linux validation passed all three focused CTests: 3,592 context assertions, 54
CLI checks including four real Linux native executions, and the standalone
linker host-object contract. LLVM 18 also cross-produced real Intel/arm64 Mach-O
objects and `ld64.lld -dead_strip` executables; all four inspect to identical
Unicode context JSON. This validates the containers and retained data, not
Darwin execution. Actual macOS CLI coverage now includes host-linker dead-strip
inspection and program execution for both NIL encodings.

The frozen source/help/test evidence is
`context-macho/final/validation.json` under the original macOS evidence root,
SHA-256 `ae848f9ee8d1e2daaee815601dbeeb6c4d8a2a7d77134f5d35d431d10b1f16b9`.
User-facing context, tool and platform docs describe the new implementation
while keeping native hosted acceptance pending. Existing Linux/Windows SDK
bytes and their earlier acceptance records are unchanged.


## Residual host fixtures and bounded process transitions

The `7e53828` native matrix has passing builds and backend selection on all four
profiles. Completed profiles exposed a second ordinary-I/O fixture that assigned
different bytes to NFC/NFD aliases, and a C++ fixture whose `-I` repository root
let `VERSION` shadow libc++ `<version>` on a case-insensitive filesystem. The
fixture now keeps a separately named protected sentinel, checks exact output
spelling, and retains failed namespace observations. Quoted-header search uses
`-iquote`; MSVC behavior is unchanged. All three affected Linux CTests pass, and
a private include-poison control fails with `-I` and passes with `-iquote`.

The retained debugger-consumer trace shows repeated termination after a timeout
signal. It does not distinguish pending leader-exit observation from a changing
group snapshot. Darwin cleanup now shares one owner deadline across signal,
exit observation, complete group proofs, retries and final reaping. It accepts
no new/live members or missing retained leader, and never signals an enumerated
PID. Independent review and Linux controls pass; native closure remains pending.
The ordinary-I/O output-budget control also retains its full failed cleanup row.

Evidence is in `followup-fixtures/` and `process-transition-followup/` under
`/home/nathan/vm/nil-win11/artifacts/macos-parity-20261105`. The latter Linux
report is sealed as
`409645774c62e977893b128f9ad663d5426e79511cad24fdd5f1c564ea8d92e3`.

A manual two-architecture `macOS host probes` workflow runs these focused suites
without rebuilding the framework. Its publication diagnostic uses private
synthetic SDK trees and preserves each failed observation while comparing
replacement, swap/unlink, swap/retention and hardlink/retention primitives.
The production publisher is unchanged: the APFS reader `EINVAL` is still open.
These diagnostics neither publish an SDK nor qualify a release.


## Native matrix and focused follow-up results

[Run 41945225536](https://github.com/exec-software/exec-nil-framework/actions/runs/41945225536)
completed at `7e53828e65444d1bd78213219e0e3fac65fcb66e`. All four build and
backend-selection gates pass. Every inventory is complete, with no collector
errors; failures and skips retain their actual status:

| Host | LLVM | Pass | Fail | Skip | Registered |
| --- | --- | ---: | ---: | ---: | ---: |
| Intel x86_64 | ON | 558 | 6 | 5 | 569 |
| Intel x86_64 | OFF | 415 | 4 | 146 | 565 |
| Apple Silicon arm64 | ON | 557 | 7 | 5 | 569 |
| Apple Silicon arm64 | OFF | 413 | 6 | 146 | 565 |

All fourteen canonical-path contracts pass on every profile. Both LLVM-ON
profiles pass 3,592 executable-context assertions and 60 CLI checks with six
native executions, including actual Darwin dead-strip linking. Intel native
inline assembly passes; the ARM native inline-assembly profile remains open.
All 44 archived tools have verified payload hashes, executable mode and matching
Mach-O architecture. These are build archives, not qualified SDK packages.
Linux source validation at the same source also passes all three CI jobs.

The raw inventories and logs are retained under
`/home/nathan/vm/nil-win11/artifacts/macos-parity-20261105/run-41945225536`.
`native-test-reconciliation.json` is sealed as
`d21f3f3c0a994327043522e8d909480726bc2fa21e06852041c132a30b7a378a`;
`artifact-tool-inventory.json` is
`17a035334821c0ef5a39cbe0b547eb9aa2bb2531175e8eb0c698e385e922c7b6`.

[Focused run 41947789948](https://github.com/exec-software/exec-nil-framework/actions/runs/41947789948)
at `f8ca6ccc1779fed7a51def8337d7d6d0960be248` passes all five host suites on
both architectures: ordinary-I/O namespace controls, process oracle, process
identity, debugger-consumer cleanup and the actual C++ parallel-process contract.
All suites verify cleanup; all 54 captured evidence files match their recorded
hashes and the tracked checkout is unchanged. The old full-run failures remain
preserved; these focused results do not substitute for a new complete inventory.

The publication diagnostic reproduces `EINVAL` during reader opens with the
production replace primitive and swap followed by unlink on both hosts. Both
swap-retain and hardlink-retain complete 5,000 replacements per host without a
reader failure, preserving held old-generation bytes. Retention is therefore a
concrete candidate for the APFS inode-lifetime problem; production publication
acceptance remains open. The focused report is
`host-probes-41947789948/reconciliation.json`, SHA-256
`478eac1b385dbc236e60a124b2dc70c7d7de9f666f44d3d6a51b2c2baaa35059`.

A separate cache audit found Darwin's compiler loaded-image identity is missing
for VM reuse as well as the already-disabled native cache path. Source dependency
hashes remain valuable and must not be discarded to represent this toolchain
gap. An explicit completeness field can disable reuse while retaining input
protection. Complete Darwin loaded-code and native link-input discovery remain
work to do; no incremental-parity claim is made.


## APFS publication inode retention

The Darwin publisher now hardlinks the old active symlink into a private,
same-filesystem sibling directory before its existing atomic replacement. It
opens the directory without following links and verifies ownership, mode,
filesystem, link type, exact inode identity and target spelling. Unsupported
linking, name collisions and invalid retention storage leave activation unchanged.
Rollback to a prior SDK uses the same path; rollback to no active SDK also
retains the just-published inode before unlinking it.

Anchors persist after successful publication, failure, rollback and process exit.
A crash can leave an extra anchor. No age/count heuristic establishes that all
readers have finished, so there is no automatic pruning. Relative targets in
these records are not consumer entry points. Cleanup requires stopped publishers
and readers. Linux and Windows production replacement paths remain unchanged.

Both root and independent peer review passed. Portable controls exercise real
POSIX link identity and failure preservation; native production acceptance is
still pending. The short hosted workflow now runs the actual complete SDK
publication contract separately from its diagnostic baselines. Unretained
replacement and swap/unlink failures remain retained negative observations.


### Restrictive-umask follow-up

The native `6322773` probe confirms production inode retention through 5,000
replacements on each architecture, while unretained negative baselines still
produce reader `EINVAL`. The separate full publication contract then reached a
new Darwin-specific defect: unlike Linux, Darwin applies a restrictive umask to
new symlink permissions, and `readlink` on the activated mode-000 link returns
`EACCES`. The failed run is retained as `41948858372`; it is not full acceptance.

Creation of the publisher-owned temporary symlink now scopes umask zero and
restores the caller mask before retention or activation. It verifies normal
mode `0777` without modifying any existing link or SDK target. Controls require
restoration before activation and after success or creation failure, with
existing inode/target modes and failed-activation identity preserved. Root and
independent reviews pass; native full-contract recheck remains pending.


## Compiler identity completeness and cache containment

Analysis now reports `compilerFilesComplete` independently of
`dependenciesComplete`. Linux loaded-file collection refuses incomplete,
deleted or unreadable mappings; Windows uses its existing module collector;
Darwin explicitly remains incomplete. Nilbuild requires explicit true compiler
identity and the file array before reuse. Older or incomplete reports still
build, but cannot authorize cached output.

Known source and tool inputs remain hashed and protected when reuse is disabled.
A changed identity-completeness result between pre-build and post-build analysis
rejects publication. This closes the Darwin VM reuse gap without pretending that
loaded-image/shared-cache or native linker discovery has been implemented.

A separate controlled compiler-identity contract checks rejection of existing
cache entries for missing, false, malformed or absent-list certification;
uncached repeated builds; consumed-input collision protection; same-mtime source
mutation rejection; and a mid-build completeness transition. The existing real
positive incremental-parity test remains active and will still fail on Darwin.
Root and independent review passed. Linux real VM/native reuse and existing
source/project contracts pass; native compiler-containment verification follows
in the next complete matrix.

The remaining ARM assembly failure is also recorded precisely: its Matrix
fixture requests x86_64 assembly and expects native success. The compiler
correctly rejects that foreign target on ARM. A real bounded aarch64 instruction
profile, with target-specific validation and native route tests, remains
unimplemented; successful parsing and VM rejection do not fill that gap.


Final Linux cache validation passes seven distinct focused contracts: 33 explicit
compiler-identity checks, 41 module fingerprint checks, 11 source-dependency
checks, and the existing analysis/project/native-toolchain/real incremental
contracts. An initial new-test failure expected the later graph-change message;
actual source digest validation rejected the mutation earlier. That failure is
preserved, and the corrected assertion requires the actual diagnostic and exact
changed path. Production code did not change for that correction.
The reconciled report is `compiler-identity-containment/linux-reconciliation.json`
under the newer macOS evidence root, SHA-256
`e658f064b3b2532cc54a69f975fabbd2a7f54c81d4178473b45954cc4dcf17b5`.


## Native publication follow-up accepted

[Run 41949695452](https://github.com/exec-software/exec-nil-framework/actions/runs/41949695452)
at `e08796410621ea06c821e57c167635e61661a985` passes the actual complete
publication contract on both Intel and Apple Silicon, including restrictive
umask, concurrent validation/rollback and strict reader observations. All six
focused host suites pass on each architecture with verified cleanup.

Production stress completes 5,000 replacements with 5,000 retained anchors on
each host: 111,658 reader opens on ARM and 98,837 on Intel. Held old-generation
bytes remain unchanged. The unretained-replace and swap/unlink negative
baselines still produce `EINVAL`; they retain explicit FAIL observations and
account for the diagnostic workflow's red aggregate. The production result is
separate and passes; no retry or error suppression was introduced.

All 64 evidence files, source pins, artifact ZIP digests and raw logs were
verified. The report is `host-probes-41949695452/reconciliation.json` under the
newer evidence root, SHA-256
`327f7576e90f83e9d68d9d2544f3cac66250ea7c5af5a0a7c9c82f1356fb0665`.
The new complete framework run is `41949698862`; installed/release SDK
qualification remains separate.


## SDK packaging audit for the next macOS stage

A read-only audit of `8d906b7` distinguishes existing format support from the
remaining distribution work. The runtime helpers already recognize Mach-O
dylibs and BSD static archives. The development consumer gate in
`scripts/check-sdk-routes.py` explicitly rejects Darwin, however, and the hosted
workflow uses `CMAKE_INSTALL_BINDIR=bin` rather than the root command/runtime
layout expected by packaging. The recorded hosted provider inventory lacks
Pandoc, which the SDK documentation builder requires.

The next stage needs a Darwin development consumer gate, SDK-root installation
configuration, Pandoc, a pre-CTest qualification snapshot, and actual staged
install/package/validate/relocation checks. Build archives containing `bin` and
`lib` are not substitutes. Staged loader closure, manifest/hash equality,
minimum OS behavior and an explicit policy for external provider/DYLD paths
must be measured on both hosts. The reviewed release profile remains Linux-only.

Findings are saved outside the repository as
`/home/nathan/vm/nil-win11/artifacts/macos-parity-20261105/sdk-packaging-audit-8d906b7/findings.txt`,
SHA-256 `4d37dc2550cc9984d4e2fbd3764e7742b5364e83432c95e2ef4c1954d3a1f0df`.
No SDK builder, installation, provider setup or release action was performed
for this audit.


## Final four-profile source checkpoint at e087964

[Run 41949698862](https://github.com/exec-software/exec-nil-framework/actions/runs/41949698862)
completed all four profiles at `e08796410621ea06c821e57c167635e61661a985`.
Every build and backend-selection gate passes; every registered test is observed
with no inventory or tracked-source errors:

| Host | LLVM | Pass | Fail | Skip | Registered |
| --- | --- | ---: | ---: | ---: | ---: |
| Intel x86_64 | ON | 564 | 1 | 5 | 570 |
| Intel x86_64 | OFF | 419 | 1 | 146 | 566 |
| Apple Silicon arm64 | ON | 563 | 2 | 5 | 570 |
| Apple Silicon arm64 | OFF | 419 | 1 | 146 | 566 |

The sole common failure is the real positive incremental-reuse contract:
Darwin now correctly rebuilds when compiler identity is incomplete. Its new
33-check containment contract and 41-check module fingerprint test pass on all
four profiles. The second ARM LLVM-ON failure is native assembly: the fixture
requests x86_64 and the foreign target is correctly rejected. A bounded ARM
instruction profile remains unimplemented. Intel native assembly JIT/AOT passes.

Every prior path, ordinary-I/O namespace, process cleanup, parallel-process and
SDK-publication failure is fixed in the full inventory. Both LLVM-ON profiles
pass 3,592 context assertions plus 60 CLI checks with six native executions,
including dead-strip retention. All four pass the 210-check execution checkpoint
contract. All 44 archived tools pass independent hash, executable-mode and
Mach-O CPU/header checks. Installed SDK, provider, desktop and release
qualification remain separate; no active SDK or Lich artifact was changed.

Source validation at documentation-only successors `8d906b7` and `9bdabd7`
also passes LLVM ON, LLVM OFF and the VS Code extension:
[runs 41950057671](https://github.com/exec-software/exec-nil-framework/actions/runs/41950057671)
and [41951456562](https://github.com/exec-software/exec-nil-framework/actions/runs/41951456562).
The intervening changes contain only documentation; runtime, compiler, tooling
and test source are identical to `e087964`. The final documentation render
builds all 67 SDK manual pages successfully.

Retained evidence is under
`/home/nathan/vm/nil-win11/artifacts/macos-parity-20261105/run-41949698862`.
`native-test-reconciliation.json` is sealed as
`3ed6c9a277a573ec9af85c6802d7cb0323b0a4a598e8753fbed7bcc380eaf3e8`;
`artifact-tool-inventory.json` is
`c4cd8d0c9cb6c6aee1a0a06de1d26e297de69943f0b1db41fdbd40fd0c57a9ca`.
All four artifacts and raw job logs are retained alongside earlier failures.


## Private Windows compiler-identity regression

A private source/build tree at exact `e087964` compiled `nilcompiler`,
`nilbuild`, `nilinterpreter` and the module fingerprint test with MSVC 14.44 and
LLVM 18.1.8. Four focused CTests pass with no skips: 64 module-fingerprint checks,
33 explicit compiler-identity/source-protection checks, 11 analysis-dependency
checks and 73 real VM/native incremental checks (181 total). Configure and
build took 4.62 and 62.88 seconds; the final test phase took 36.80 seconds.
All owned-process cleanup and source/input/binary seals were verified. The
existing checkout, accepted SDK, VSIX and machine/user settings were unchanged.

Earlier private attempts are retained rather than relabeled. V1 omitted the
existing explicit DIA-library override; v2/v3 configuration succeeded but the
strict process check identified a remaining owned `mspdbsrv.exe`. Selecting
`CMAKE_TRY_COMPILE_CONFIGURATION=Release` for this Release build resolved it
without a cleanup exception. V4 built successfully; an extra diagnostic image
observer encountered access denied on an exiting member. V5 reused sealed V4
binaries and ran the actual contracts with the original strict ownership checks.
There was no source fix, rebuild or ownership relaxation for that observer.

Evidence is under
`/home/nathan/vm/nil-win11/artifacts/macos-parity-20261105/windows-cache-e087964`.
`windows-reconciliation.json` is sealed as
`01b1b084824d3fb1e50b97d1869eb05eb6cccb63d62e5f056241e700146929af`;
the final raw report is
`7cdd5a8a55984130ce16f544771fd94c9855cc1f4b6e9c1537710d91f47e00e7`.
This verifies the common cache-contract change on Windows, not a replacement
for the complete paired `5630388` SDK acceptance.


## Next parity pass: native cache evidence, ARM assembly and SDK staging

The next pass keeps the completed `e087964` baseline and advances three separate
surfaces: a bounded `aarch64` inline-assembly profile, complete Darwin compiler
and linker identity, and isolated macOS development SDK staging/relocation.
The existing safe no-reuse behavior remains in place while cache discovery is
investigated; neither unavailable backing files nor shared-cache UUIDs alone
may authorize reuse.

The manual `macOS cache identity probes` workflow collects own-process loaded
image observations, availability-checked dyld snapshot SPI, actual shared-cache
backing file paths and bounded byte hashes, plus a native driver/linker dependency
record. It records incomplete observations and source/host identity explicitly.
Hashing is limited to 16 GiB and 120 seconds; the complete diagnostic has a
420-second limit. This does not build an SDK or enable cache reuse. Nine pure
parser/file-bound controls and root/independent workflow reviews pass; actual
native API and backing-file behavior will be measured on both hosted CPUs.

API signatures and snapshot lifetimes were checked against Apple's
[dyld introspection declarations](https://github.com/apple-oss-distributions/dyld/blob/main/include/mach-o/dyld_introspection.h)
and [public dyld declarations](https://github.com/apple-oss-distributions/dyld/blob/main/include/mach-o/dyld.h).
The public callback storage has process lifetime because those callbacks cannot
be unregistered. SPI absence, inconsistent snapshots and inaccessible files
remain explicit; no production dependency on those APIs is introduced here.


## Bounded AArch64 assembly implementation

Ny and Matrix now accept the explicit `aarch64` target through source, textual
and binary NIL. The 14 listed instruction spellings operate on i64 registers,
with at most eight read-only inputs and one early-clobber output. Shift/rotate
counts are canonical `#0..#63`; ordinary i64 inputs supply other constants.
Output reads require a previous initialization. LLVM checks the exact host
architecture and uses target-specific clobbers. The NIL schema is unchanged.
Interpreters retain and reject the opcode rather than silently omitting it.

The validator passes 351 checks. LLVM 18 emits all 19 fixture templates as ELF
and Mach-O objects, and both frontends round-trip the ARM source through text
and binary NIL with interpreter rejection. Four focused Linux CTests pass,
including eight native Ny/Matrix executions, Matrix interpreted/native routes,
and foreign-target or malformed-input rejection with previous output preserved.
An initial loader failure exposed an old x86-only metadata gate; that failure
and the subsequent passing run are both retained. These results establish
source/validation behavior and x86 regression coverage, not ARM execution.
Native Apple Silicon execution and a Windows regression are the next gates.

Implementation and LLVM constraints received independent review. Evidence is
under `/home/nathan/vm/nil-win11/artifacts/macos-parity-20261105/aarch64-inline-assembly`;
`handoff.json` is sealed as
`c47864d77e7fc42d9f39a2839190a3adaa1888999ef7777cd43a27551967a8e4`.
The test registration now selects the host's actual instruction profile and
requires eight native executions when LLVM is enabled; no skip substitutes
for a native route. Installed SDKs remain at the accepted `5630388` baseline.


## Native cache-identity observations

[Probe run 41955423846](https://github.com/exec-software/exec-nil-framework/actions/runs/41955423846)
completed both native hosts at exact `2656d1664b7321f3fb5e1647befc45585094a891`.
The workflow fails explicitly with `INCOMPLETE` observations on both CPUs;
source, capture cleanup and all 72 retained file digests reconcile. All ten
dynamically checked introspection entry points were available. Initial public
loaded-image counts grow from 44 to 307 after initializing introspection. Each
individual public capture is generation-stable, but the before/after graphs
differ and 307 SPI images have null per-image backing paths. Missing
paths are not evidence of complete cache membership.

The active root-cache UUID matches the root file header. Full byte hashes were
collected for two backing files on ARM (4,915,642,368 bytes in 30.39 seconds) and
seven on Intel (4,979,867,648 bytes in 112.20 seconds). Subcache UUID-table
correspondence is not yet established. These costs already make whole-cache
hashing inappropriate for routine incremental checks, before considering the
compiler's current scalar hash implementation. The follow-up investigates
validated image membership/ranges, backing metadata and bounded hash cost;
no production identity guard is relaxed.

The actual native link records contain 80 input records representing 42 unique
files on each host, with 194 failed searches on ARM and 278 on Intel. A couple
of top-level SDK stubs cannot describe the full native input graph. Compiler,
driver and linker loaded dependencies and selection inputs also need coverage.
The original safe no-reuse result remains in force for both VM and native
Darwin builds.

Evidence is retained under
`/home/nathan/vm/nil-win11/artifacts/macos-parity-20261105/cache-identity-probe/run-41955423846`;
`reconciliation.json` is sealed as
`7444bc72adf84e232cb1a4026e9d69118f0723f70e71d479bbc77e47620da4c8`.
Its PASS describes raw-evidence replay only; both native diagnostic outcomes
remain INCOMPLETE.

The next bounded diagnostic is designed separately: positive cache-membership
observations, root/subcache mapping and slide-table correspondence, selected
on-disk image ranges with fixup/zero-fill distinctions, a 128 MiB scalar versus
platform SHA comparison, and direct driver/linker loaded-library observations.
Compiler-specific probes must also include the actual NIL runtime link inputs.
A range identity would need its own explicit schema and complete before/after
validation; it cannot be represented as an ordinary whole-file identity.
The design remains outside production code until both completeness and cost
are established.


## Windows assembly regression at 8517ad0

A private MSVC/LLVM build at exact
`8517ad0288700187a3714042ea259f8cf5e67773` passes all four focused CTests:
351 validator checks, eight native Ny/Matrix executions, four interpreted
Matrix routes and two native Matrix routes. The complete ARM fixture also
round-trips through both frontends and text/binary NIL with the expected
interpreter rejection. Foreign ARM targets are rejected by x86 native routes
while preserving existing destinations. This proves Windows regression and
serialization behavior, not native ARM execution.

The 12 owned phases verified process cleanup; the build took 63.5 seconds and
the bounded run completed in 100.03 seconds. Exact source/input/tool seals and
the accepted `5630388` SDK manifest plus all 13 tool/runtime payload hashes
were unchanged. No active SDK, VSIX, guest checkout or settings were replaced.
A failed initial multi-remote evidence copy is retained; separate file copies
succeeded without rerunning the native tests.

Evidence is retained under
`/home/nathan/vm/nil-win11/artifacts/macos-parity-20261105/windows-asm-8517ad0`;
`windows-reconciliation.json` is sealed as
`3bcabc466559e1e2794a882adbc918bd33fb5358196b91978b7934323b5d3a00`.
The reconciliation verifies 68 copied evidence files and 1,637 source files.


## Isolated macOS development SDK pipeline

The manual `macOS development SDK` workflow builds native LLVM-ON candidates
on Intel and Apple Silicon. It selects a root tool layout, records an exact
pre-CTest build snapshot, retains the complete test inventory, and uses the
existing development-package rules without changing the Linux-only RC gate.
A failing test still fails the job. Packaging and diagnostics can continue
after a complete failed inventory so their results are available independently.

Pandoc 3.13 comes from hash-pinned official per-architecture release assets.
Build and installed Mach-O loader metadata are identical, so existing tested
versus packaged byte-equality checks remain in force. Candidate validation is
followed by relocation outside the checkout to a space/Unicode path, direct
loader metadata observations, actual shared ABI and loader-path checks, and
the full 1,184 source/binary-library consumer executions. A private conflicting
sibling library supplies a negative loader control. Candidate bytes and modes
are rechecked after use; partial failures remain diagnostic evidence.

Darwin installed consumers now admit native x86_64 and arm64 development hosts.
Their environment removes source/library overrides and Apple compiler/SDK
selectors, and records the selected default toolchain. External Homebrew and
Apple prerequisites remain explicit. This workflow neither activates an SDK
nor publishes to Lich, and it does not establish a portable or release-qualified
macOS package. Native results must be recorded separately from this pipeline
implementation.

The first full Linux source-validation run at `8517ad0` exposed one stale
rejection golden: it expected the previous x86-only diagnostic. The expected
stderr now matches the two-target diagnostic exactly. The focused route CTest
and a direct compile-rejection/output-preservation check pass; the original
full-suite failure log and before/after evidence remain retained under
`aarch64-inline-assembly/rejection-golden`. No compiler behavior was weakened.

The final staging workflow passed 12 host controls and 14 consumer controls,
Python/YAML/shell checks and strict C++ syntax checking for its shared consumer.
Independent reviews cover the actual loader-path/ABI control, environment
selectors, external dylib aliases, failed-inventory retention and partial
candidate evidence. The five-file validation report is sealed as
`628e72b3816232679b8f0b415da6e151f87ccd195b66c69a66fcaee30eb20dd8`
under `macos-parity-20261105/sdk-staging-workflow-controls/report-final.json`.
These are workflow controls, not native SDK acceptance.


## First SDK run: official ARM documentation-tool size

[Run 41957412951](https://github.com/exec-software/exec-nil-framework/actions/runs/41957412951)
started at `1cfe0bf877cd0eb38665a45156194847780b6374`. Intel passed Pandoc
and proceeded to its native build. ARM stopped before configuring NIL: the
correctly hash-pinned official Pandoc ZIP contains a regular 0755 executable
of 195,370,176 bytes, above the initial 160 MiB extraction ceiling. The original
failure, ZIP, eight evidence files and clean source identity are retained.

The finite executable ceiling is now 256 MiB; hash, expected member path and
regular-file checks are unchanged. Thirteen staging controls pass, including
the actual ARM ZIP metadata and boundary cases. Extraction of the retained
official ZIP succeeds with binary SHA256
`8f17378a70e46eed7b63b5433fff6f5a0802f92659aa5782418548e0ca4a8e44`;
that Linux check does not execute the macOS binary. The independently reviewed
report is sealed as
`7a178dfae7df4aa068290e98f8e25d1f55bf3f0837da8c29fb532975821d8403`
under `macos-parity-20261105/pandoc-arm-bound-20261105/report.json`.

The workflow now accepts an explicit `both`, `x86_64` or `arm64` native host
selection, defaulting to both, so an ARM-only retry can preserve the running
Intel job. The selector's exact runner matrix and all shell bodies are checked.
This correction changes documentation-tool admission, not language behavior.


## Bounded cache-detail diagnostic

A separate manual `macOS cache detail probes` workflow now collects cached
install-name/UUID correspondence, bounded root/subcache metadata and selected
segment-range observations, selected driver/linker loader observations, and
128 MiB prefix hash timings. The original whole-cache probe is preserved.
Twenty-nine malformed metadata/range/correspondence controls pass; strict C++
syntax was checked with Linux declaration stubs, which does not prove native
Darwin availability or execution. Independent review covers the primary Apple
API declarations, process ownership, finite bounds and result interpretation.
The local control report is sealed as
`6f19e5849d4303921a5f44a3fcedd52fcbeeea57d076893ee3fa96ced12c671c`
under `macos-parity-20261105/cache-identity-probe/detail-local-controls`.

`OBSERVED` means diagnostic collection only. Both `loaded_backing_complete`
and `identity_complete` remain false. Cached names/UUIDs can also appear in an
overridden or copied image; matching them does not prove active cache backing.
Nominal segment ranges leave file-size/zero-fill, slide/fixup and transitive
loader metadata unresolved. Prefix hashes compare the current scalar SHA with
CommonCrypto, scalar first and platform second, including I/O and warm-cache
effects. They do not establish whole-file identity or a normalized benchmark.
No production compiler/runtime closure or cache eligibility changes are made.

## Remaining native acceptance work

A read-only audit identifies the five existing LLVM-ON macOS skips precisely:
PostgreSQL client and server acceptance, desktop UI, and the managed/owned
lifetime stress tests. The last two require a proven enforced memory cap.
Current XNU sources implement `RLIMIT_AS`, but the exec path's handling means
a launcher-level limit round trip alone is insufficient. A native probe must
prove actual post-exec allocation rejection and include a too-small-startup-cap
control before admitting Darwin. RSS sampling is weaker for these calloc-heavy
fixtures because reserved pages need not all be touched. Existing skips remain.

Installed debugger acceptance also needs its complete relocated-SDK consumer
with an explicit origin manifest and the same clean Apple environment. The
source DAP tests alone do not replace it. PostgreSQL needs a separately recorded
provider/runtime/server profile; a macOS display backend and isolated desktop
fixture remain implementation work. Audit evidence is source-pinned at
`macos-parity-20261105/native-lifetime-next-gaps-4181096`; `report.json` is sealed as
`9ac11a74e9eeb36225876b5a79d95edb5b0135b4d4bd344ec96753794cec1553`.

The memory-cap review uses Apple's macOS 15 XNU sources for
[resource limits](https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.140.69/bsd/kern/kern_resource.c),
[VM mapping enforcement](https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.140.69/osfmk/vm/vm_map.c),
and [exec limit application](https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.140.69/bsd/kern/kern_exec.c).
Those source observations are not native execution evidence.


Full Linux source validation now passes at exact
`4181096aef65e3ada5a7085005b10a06ca3d2469`: LLVM ON, LLVM OFF and the VS Code
extension all succeed in
[run 41957858527](https://github.com/exec-software/exec-nil-framework/actions/runs/41957858527).
This includes the corrected exact rejection diagnostic. Both failed `8517ad0`
job logs remain retained; their sole failed CTest was that stale golden.


## Native SDK failures and corrective pass

ARM SDK retry
[41957867092](https://github.com/exec-software/exec-nil-framework/actions/runs/41957867092)
at exact `4181096` completed all 570 registrations: 561 passed, four failed,
and five skipped. Besides the known incremental-reuse limitation, native Ny
and Matrix assembly returned incorrect values, and the checkpoint client
timed out writing its deliberate 62,000-character invalid field. Build, install
and all documentation generation completed; package qualification rejected
a changed static archive, so installed validation and consumers did not run.
The 362-file evidence reconciliation is sealed as
`1a92410d0c9daf8405c57757be01e7ec245974e8a77c013ed694717c155855ae`
under `macos-parity-20261105/sdk-run-41957867092/arm64-reconciliation.json`.

The ARM output identified a real lowering bug: Darwin's AArch64 assembler
treats semicolons as comments. Seven multi-instruction fixture blocks executed
only their first instruction. After contract validation, the native ARM copy
now converts instruction separators to newlines, preserving the original NIL
template and x86 behavior. Before/after disassembly of all 20 exact templates
shows the corrected Mach-O instruction words match their ELF counterparts;
ELF output is unchanged. Four focused Linux CTests still pass. Expected
application output was not changed. Cross-target evidence is sealed as
`003b01800923af1c68345e854448bd5b08b6d52f91f84096fc82e32a2c5db0cb`
under `aarch64-inline-assembly/separator-fix/report.json`. Native ARM rerun
remains required.

The archive rejection also remained strict. All eleven installed tools and
the shared runtime matched their pre-test bytes. Only the BSD archive-index
timestamp changed; restoring those 12 bytes in an in-memory diagnostic copy
reconstructs the pre-test SHA exactly. The workflow now uses Apple's
`ZERO_AR_DATE=1` for both build and install. No artifact is rewritten after
testing, and the exact-byte gate is unchanged. All 45 qualification controls
pass; native installation must confirm stable bytes.

The checkpoint test client had incorrectly reused its 0.1-second read-poll
timeout for a complete socket write, while Windows pipes already used five
seconds. Socket writes now use the same bounded frame budget and restore the
read-poll timeout in `finally`. Real socket backpressure reproduces the old
failure; delayed-reader, stalled-peer and closed-peer controls pass after the
fix. The real Linux checkpoint contract and both focused CTests pass. The
three channel controls are registered on UNIX hosts; production IPC is unchanged.
All three fixes received independent review before the next native SDK run.

## Completed native cache-detail observations

[Run 41959228157](https://github.com/exec-software/exec-nil-framework/actions/runs/41959228157)
at exact `4a888e26dc79ea0336e841b4c95bc64f2702180d` completed on both hosts.
All 29 controls, 18 owned phases and source/cleanup seals pass per architecture.
Independent replay verified all 46 retained files per host. Root/subcache
UUID, VM-offset and API file-set correspondence passed for one ARM subcache
and six Intel subcaches. Both runs observe 308 loaded images and 307 cached
name/UUID matches; active backing and whole content identity remain incomplete.
Selected nominal ranges cover 13 ARM and ten Intel segments without proving
zero-fill/fixup coverage. Driver/child-linker traces also captured libLTO.

For 128 MiB prefixes, scalar/CommonCrypto times were 1.34/0.07 seconds for
the ARM root and 1.34/0.08 for its child; Intel measured 4.01/0.68 and 4.57/0.66.
Each pair has equal digests, but scalar runs first and both measurements include
I/O. Host OS builds differ (Intel 15.7.9/24G830; ARM 15.8.2/24H232), so these are
observations rather than normalized benchmarks or whole-cache projections.
No production cache eligibility changed.

The independently reviewed reconciliation is sealed as
`5baa141a685926b775575a5e393b4e5d3b799b77f5c5b2880b3e6b5e16b65060`
under `macos-parity-20261105/cache-identity-probe/detail-run-41959228157`.


The original Intel SDK run at `1cfe0bf` also completed: 563 passed, two failed
and five skipped out of 570. All assembly contracts pass there. Besides the
known incremental limitation, the fault-managed-lifetime fixture's direct
text and binary interpreter routes hit their ten-second deadline. One had
printed all nine expected lines; both bundled routes and both native routes
passed, with bundled executions below one second. The large timing difference
remains unexplained; it is not evidence of a corrected runtime defect. A
fixture-specific 30-second retry budget preserves exact output and exit checks
and leaves other route deadlines unchanged. The next native run must actually
complete those routes; a further timeout remains a failure.

The complete Intel evidence reconciliation is sealed as
`7138470a3757efc0621d8588d9f6ec27704852971f3b2f07bed4873295c84664`
under `macos-parity-20261105/sdk-run-41957412951/intel-reconciliation.json`.
Its package step also failed before installed validation. No first-run SDK
is accepted or substituted for the installed Linux/Windows baseline.


## ARM execution confirmed at f96f237; strict snapshot binding corrected

The ARM profile in
[run 41960707317](https://github.com/exec-software/exec-nil-framework/actions/runs/41960707317)
completed all 571 registrations at `f96f2378bac888a6d06af4cc829f7cbd5aa51de5`:
565 passed, the single known positive cache-reuse contract failed, and five
tests skipped. This includes 351 assembly validator checks, eight actual
Ny/Matrix ARM native executions through text/binary object linking, JIT and
AOT, two additional native Matrix routes, and four interpreted Matrix rejection
routes. The real checkpoint contract, all three socket-channel controls and all
six fault-managed-lifetime routes also pass. Native execution, not cross-object
emission alone, now confirms the separator correction.

The pre-test SDK snapshot correctly refused a registration/catalog mismatch:
the lifetime fixture now requested 30 seconds, but its positive-proof catalog
only admitted the historical ten-second default. Therefore no snapshot, tool
payload fingerprints, installation or consumers were produced by this ARM job.
Its 346-file evidence reconciliation is sealed as
`9d45a32126e76c855393e838aca3b0926b7a10010a188eef32881cc16ad67220`
under `macos-parity-20261105/sdk-run-41960707317/arm64-reconciliation.json`.
These are complete native source-test results, not an accepted SDK.

The catalog now declares an optional integer `timeout_seconds: 30` only on
that exact fault-managed-lifetime proof. The command must match it exactly;
missing, changed, duplicate and undeclared timeout options remain errors.
Catalog entries without the field keep their historical ten-second meaning,
so older manifest evidence is not reinterpreted. The catalog hash changes
explicitly, and source/registration/report/manifest bindings remain enforced.
Twenty-four capability controls and 45 qualification controls pass. Replaying
the actual local CTest listing first reproduces the old error and then binds
all 31 proofs correctly. This matches the native snapshot error. A cheap
registration preflight now runs immediately after configure, while the exact
pre-test built-payload snapshot remains mandatory.

Binder evidence is sealed as
`4a19eb6b3071602994d68318a8cdb38faebf16c1865ee0f3705797e866849890`
under `macos-parity-20261105/sdk-capability-timeout-fix/report.json`.
The new catalog canonical SHA is
`4b43113c6e3ae51debe2ff8ba8c9352ddb3ea1ce148d29637cb67cdc4efbaad8`;
old validators deliberately reject the new optional shape.

The same `f96f237` source also passed a fresh private Windows build and five
focused CTests, including the real checkpoint contract. It retains the 351
validator checks, eight native executions, six Matrix routes and four complete
ARM text/binary serialization/rejection checks. All 12 owned phases cleaned
up, and the accepted SDK's 13 tool/runtime files, manifest, source and helper
inputs were unchanged. Its independently replayed 68-file evidence is sealed
as `20f6c2ba2f21cb02b7b4acbeb6b4aa9ce485169afb5de164b3c3f78c6a06cbde`
under `macos-parity-20261105/windows-asm-f96f237/windows-reconciliation.json`.
No installed SDK or settings were replaced.


## Bounded native memory-cap diagnosis

The two native lifetime tests remain excluded on Darwin until a meaningful
memory bound is demonstrated. Modern XNU has an `RLIMIT_AS` implementation,
but successful `setrlimit`/`getrlimit` alone cannot establish that an executed
program retains the bound. Exec startup mappings and allocator reservations
must be observed directly; RSS sampling would miss reserved, untouched pages.

The separate manual [memory-cap workflow](../.github/workflows/macos-memory-probes.yml)
runs small owned children on Intel and Apple Silicon. It preserves the fixed
96 MiB profile and reports setup rejection, exec failure, inherited limits and
actual post-exec reservation/allocation outcomes separately. A second, explicitly
named current-map-plus-64-MiB profile is diagnostic only. Neither that profile
nor the diagnostic workflow admits a production lifetime test. A child with
192 MiB of untouched BSS probes the exec startup caveat. Allocation/free reuse,
retained mmap and retained calloc controls distinguish useful enforcement from
an unusable cap or an unbounded allocator.

Every child has a 20-second deadline; work stops at 160 seconds with 20 seconds
reserved for cleanup and evidence. Single virtual reservations stop at 256 MiB,
retained allocations and physical touches at 128 MiB per child. A deliberate
owned-child timeout checks cleanup. Raw observations, exact source/binary
fingerprints and partial failure evidence are retained. `OBSERVED` means the
outcomes were collected; every report retains `qualification: false`.

Twenty-three local classification/bound controls pass, as do strict C++ syntax
checks against temporary Darwin API stubs. Those checks do not establish native
compilation or enforcement. Independent review passed; the local report is
sealed as `7daec76103a145681f5ee333ab81f3cf57e8cdc45db0288947491539d798a07c`
under `macos-parity-20261105/memory-probe-local-controls/report.json`.

The corrected runtime at `f96f237` also completed Linux Source validation
[41960686724](https://github.com/exec-software/exec-nil-framework/actions/runs/41960686724):
LLVM ON, LLVM OFF and the VS Code extension all pass. The paired macOS SDK retry
[41962386089](https://github.com/exec-software/exec-nil-framework/actions/runs/41962386089)
targets `2e098f1` with the strict registration/catalog fix. Its installed-package
outcomes remain pending.


## Native memory observations and explicit allocator follow-up

[Memory run 41963597498](https://github.com/exec-software/exec-nil-framework/actions/runs/41963597498)
completed at `07ff9d8` on both architectures. Each profile retained 43 files,
23 controls and 18 owned phases, including the deliberate two-second timeout
and verified cleanup. Independent replay checked every case classification,
source/binary hash and Mach-O architecture. The reconciliation is sealed as
`8dd154a7cce4bf997b8fbccddfb6c79b6618af1248ce4c20f937a277f9aec708`
under `macos-parity-20261105/memory-probe-run-41963597498/reconciliation.json`.

All four fixed-96-MiB cases rejected setup with `EINVAL` on each host. The small
probe's measured startup virtual maps were 420,447,207,424 bytes on ARM and
34,939,764,736 bytes on Intel. These are virtual mappings, not resident memory.
The separately named current-map-plus-64-MiB profile rejected over-headroom
reservations on both hosts. Intel also completed allocation/free reuse and
rejected retained mmap/calloc after successful allocations. ARM failed its first
malloc after one successful mmap and rejected calloc before any success; its
allocator controls therefore remain unproven. Neither result admits the fixed
96 MiB lifetime tests.

The padded executable really contained at least 192 MiB of zero-fill storage,
but its observed post-exec map was smaller than the launcher map. This left
402,685,952 bytes of nominal headroom, above the diagnostic reservation bound.
The probe correctly returned `INCONCLUSIVE_RESERVATION_BOUND`; it did not prove
or disprove the exec reapplication caveat and did not increase its allocation
ceiling to force a result.

A follow-up now exposes an explicit `nano-off` allocator profile that applies
`MallocNanoZone=0` only to owned child processes. Apple's
[nano allocator source](https://github.com/apple-oss-distributions/libmalloc/blob/main/src/nano_malloc_common.c)
recognizes that environment override. The hypothesis is that startup allocator
reservations contribute to the large maps; a native result is still needed.
The default and alternate runs remove inherited `Malloc*` options, record their
exact requested profile, and retain all original cap/classification/bound checks.
This is a diagnostic choice, not a new production setting or lifetime-test
admission. Twenty-four local controls pass, including environment isolation and
rejection of an unknown/automatic fallback profile.

The older paired `f96f237` SDK attempt is also complete: Intel matches ARM at
565 passed, one known cache-reuse failure and five skips out of 571. All six
fault-lifetime routes now complete, with Intel direct text/binary executions
at 1.402/1.195 seconds and exact output/exit checks. Intel reconciliation is
sealed as `80d867962f9119cda54006c82d44e70948053652b81539d8c5d8a8718623b45b`.
Both profiles stopped before SDK packaging at the already-corrected catalog
binding mismatch. The replacement `2e098f1` run has passed its ARM build and
pre-test snapshot; full installed results remain pending. Linux Source validation
[41962360138](https://github.com/exec-software/exec-nil-framework/actions/runs/41962360138)
passes LLVM ON, LLVM OFF and the extension for that revision.


## First packaged ARM candidate and installed acceptance expansion

The ARM job in [41962386089](https://github.com/exec-software/exec-nil-framework/actions/runs/41962386089)
at `2e098f1` reached exact-byte SDK packaging, staged validation, relocation,
relocated validation and a real shared-runtime ABI/loader check. It contains
eleven tools, eighteen compiled modules and 67 HTML pages. All 260 payload files
plus the manifest retain their exact bytes/modes, and all eleven tool hashes plus
both runtime hashes match the pre-test snapshot. This confirms the deterministic
archive fix with real installation. Its source inventory remains 565 passed,
one known cache-reuse failure and five skips out of 571.

The malformed-sibling negative control actually rejected the correct library,
with exit -6, empty stdout and verified cleanup. Darwin's error was
`slice is not valid mach-o file`; the assertion only admitted two other known
spellings. The retry adds that exact observed spelling, preserving the required
nonzero exit, empty stdout, exact malformed sibling path, timeout/cleanup checks
and unchanged candidate seal. The control reproduces the retained 834-byte stderr
and its SHA `a72e36b02efccdda53dc8c69dfb4387a7a5d6e150eca598f609a451b38e6d5d8`;
missing-library, wrong-architecture and permission errors remain rejected.
Fourteen staging controls and independent review pass. The original failed
result remains preserved; none of the 1,184 installed consumers ran in this job.

ARM reconciliation is sealed as
`a265af26cb4944f61db59401e7b309496f2dd212356dcd48612e70fb47e71f53`
under `macos-parity-20261105/sdk-run-41962386089/arm64-reconciliation.json`.
All 433 collector files and twelve actual Mach-O headers/load-command records
were replayed. Homebrew zstd remains an external prerequisite; this is not a
portable dependency closure or a release-qualified SDK.

The next SDK run adds installed debugger acceptance, bound to an external
manifest anchor recorded before relocation. A private copied consumer exercises
146 named checks, twenty exact compile/run commands and fifteen retained JSONL/DAP
sessions. It separately runs the real interpreter capture/mutate/reusable restore/
continuation contract: the scene demo's persisted state file is not an execution
checkpoint. Source, SDK, origin and raw transcript seals are checked after failure
as well as success. Darwin uses a clean Apple toolchain environment; Linux and
Windows behavior is preserved. Fifteen helper controls pass; the existing consumer
suite has 31 passes and five platform skips on Linux. Independent review passed.
Local evidence is sealed as
`6e97fa2c8e6b338c38f4e2c025e314172c404ae33186eaae657eece33b94b849`
under `macos-parity-20261105/installed-debugger-helper-controls/report-final.json`.
Native installed debugger acceptance is still pending.

The optional `private16` SDK profile builds the hash-pinned official PostgreSQL
16.15 release in a private runner directory with two build jobs and a twelve-minute
whole bound. It uses one coherent header/client/server/share installation;
preflight binds `PG_VERSION_NUM`, `PQlibVersion` and the actual `dladdr` target.
A wrong-library negative control and full before/after install manifest remain
mandatory. The provider enables a private Unix-socket test profile, with optional
TLS/GSS/ICU/readline/compression extras disabled; it does not claim those features
or start a service. Standard CTest then runs the existing real client deadlines,
provider lifecycle and six routes. Installed consumers additionally require all
six routes from each of the SDK's source and binary library roots. Their explicit
provider environment and unchanged SDK/provider declarations are recorded even
when a route fails.

Sixteen provider controls and independent review pass. The official archive has
9,094 members and 161,404,380 expanded bytes. Extraction preserves pinned release
mtimes so generated parser files stay newer than their grammar inputs; it does
not regenerate the release with an incidental runner Bison. Extraction evidence
is sealed as `58f059a055aee69c1d3be55b70fd20e0a3cd293079843840a65c01ab6bca67cd`
under `macos-parity-20261105/postgresql-provider-16.15/extraction-validation-v2.json`.
The native provider build and routes remain pending.

The separate `nano-off` memory follow-up
[41964342238](https://github.com/exec-software/exec-nil-framework/actions/runs/41964342238)
completed at `fa4370a`. Startup maps dropped by 512 MiB, but the fixed-96-MiB
setups still reject with `EINVAL` on both hosts; ARM's measured-headroom allocator
controls remain unproven, while Intel's helper-only results remain positive.
The padded child did not reach its entry point (ARM exit -9, Intel -6 with a dyld
shared-cache/libc++ mapping error); neither outcome proves the exec-setter caveat.
All 43 files per host, eighteen owned phases and 24 controls were independently
replayed. Reconciliation SHA is
`4633a9a752062d46f617d2bb65b8bb70dcdfe3f427ee9129cb532236f8df77b8`.
No lifetime tests were admitted and no production allocator setting changed.


## Expanded profile preflight corrections

The initial private-provider SDK attempt at `1c9d361`
([41965629303](https://github.com/exec-software/exec-nil-framework/actions/runs/41965629303))
stopped in a pure debugger fixture on both hosts: expected temporary paths used
`/var` while the production guard correctly returned canonical `/private/var`.
The test-only `4ab43f1` correction adds an explicit parent-directory alias case
and preserves the SDK-overlap rejection. Sixteen debugger controls now pass.
Eight retained files per host reconcile to
`e6552c065b75b89e6cea714f2f392fb88fd5017a11ac8199a9c3384e22a9d4d8`.
No provider or framework build occurred in that attempt.

The retry
[41965922556](https://github.com/exec-software/exec-nil-framework/actions/runs/41965922556)
passed all initial controls and pinned Pandoc preparation, then stopped before
provider download/configure because the standalone provider helper had not bound
`sdk_qualification`'s sibling Python imports. Reconciliation of sixteen files per
host is sealed as `1a5e4922a0ad198c3372e0d7acc1e9337758242f0e8001557ce73aed98a7e65d`.
The helper now resolves those modules from the exact checkout, with a fresh
isolated-interpreter control from an unrelated working directory. Initialization
also runs inside the failure-report boundary, retaining its actual phase and
zero completed processes if a dependency import fails. Eighteen provider controls
and independent review pass; native provider/build/installed outcomes remain pending.

The earlier `2e098f1` Intel SDK has now independently reconciled to the same
packaging/relocation/shared-loader progress as ARM: all 260 payload files plus
manifest and all tested tool/runtime bytes match. Its only source failure remains
incremental reuse, and the malformed-library negative oracle failed on the same
already-corrected Darwin wording. Intel reconciliation SHA is
`16f49141279985eec927b8860024041d6e8a00ee1f7ba182cfa018ce546c4396`.
The full installed consumer campaign did not run on either `2e098f1` host.


The next attempt, [41966299989](https://github.com/exec-software/exec-nil-framework/actions/runs/41966299989)
at `2a75b5d`, reached native PostgreSQL configure on both hosts. Its pinned parser
rejects `--without-ssl` because `--with-ssl` requires a library argument; the
supported boolean `--without-openssl` preserves the intended TLS-disabled local
profile. Both original failures retain 34 files per host, five owned phases,
verified cleanup/source seals and all initial control passes. Reconciliation SHA
is `23ebe0f84679f3d7f31ef5d50113b278ae39f0b0ec599502507b3582dd447d4b`.

The corrected complete option list also passed one bounded Linux configure-only
run against the exact pinned release in 8.21 seconds, with empty stderr, unchanged
release sources and verified cleanup. Its generated header confirms 160015 and
all eleven intended optional features disabled. No make, install or server ran.
The shared argument builder differs only by the Apple SDK path in the native
profile. Eighteen controls and independent review pass. Linux configure evidence
SHA is `0944003c66c7738b40c24f9624d9f4a2c5c8daf5a2580697a44b228dd6401021`
under `macos-parity-20261105/postgresql-provider-16.15/configure-linux-v1/report.json`.
Native provider compilation and SDK consumers remain pending.


## Native provider builds and UI prerequisites

At `2f5d695`, [41966883934](https://github.com/exec-software/exec-nil-framework/actions/runs/41966883934)
now completes the real PostgreSQL build/install, matching 160015 header/runtime
and canonical loaded-library checks on both architectures. Framework builds and
full CTest are underway; final artifacts and installed consumers still need
reconciliation. Source validation
[41966877165](https://github.com/exec-software/exec-nil-framework/actions/runs/41966877165)
passes LLVM ON, LLVM OFF and the VS Code extension.

A readiness review caught an installed-debugger helper field mismatch: SDK
manifests use `source.worktree_dirty`, while pre-test snapshots use `source.dirty`.
The helper now requires the actual manifest field, and a new control passes
through `sdk_integrity.verify` rather than inventing its output shape. Seventeen
controls and independent review pass; schema-fix evidence SHA is
`d67a6bd424a194ad9345564d748795466fe88816011a3c6c7cb5ee7895715eeb`.
The already-running SDK job retains its original source and may fail that helper
safely before debugger execution. Its independent database/standard consumer
phases can still run. Corrected debugger acceptance should replay the exact
sealed candidate with distinct SDK/helper revisions, rather than silently
changing a running checkout or assuming a newer compiler was tested.

The remaining UI port needs an Apple library-loader branch, main-thread GTK
initialization and a Quartz interaction harness; existing widget/event/lifetime
logic can be reused. Current Apple CMake selection reaches the Xvfb harness and
returns its explicit unsupported-platform skip. Retained runner inventories have
GLib, Cairo, HarfBuzz and common image/font dependencies but lack FriBidi, Pango,
GdkPixbuf, Graphene, libepoxy, GTK4 and Meson. GTK 4.18.6 requires libepoxy even
when selecting its Cairo renderer; there is no supported OpenGL-disable option
that removes that dependency. A later private build must account for the full
graph rather than implicitly downloading missing packages.

The first [UI prerequisite workflow](../.github/workflows/macos-ui-probes.yml)
therefore performs only native installed-dependency and own-window Cocoa probes.
It checks actual pkg-config headers/linking/runtime versions and loaded paths for
GLib, Cairo-Quartz and HarfBuzz/CoreText, records missing future dependencies, and
runs one owned NSApplication/NSWindow. The application queues its own keyboard
event to its own view, invokes its own close action and verifies closure.
Accessibility/input capability queries do not prompt or alter policy. No global
or process-external input, screen capture, GTK build, runtime or CMake changes
are involved. Event-loop feasibility does not establish external GTK interaction
acceptance. All results retain `qualification: false`.

The diagnostic is bounded to 180 seconds with a twenty-second cleanup reserve;
each child has at most thirty seconds. Raw observations, source/tool bindings and
load-origin hashes retain their observation phase; post-load hashes are not
presented as pre-load anchors. Thirteen controls and independent review pass;
local evidence SHA is `4f4b19796bae6d3665240b269942cc8b13391d2df9ad2515451d5d5ea37af89f`
under `macos-parity-20261105/darwin-ui-audit/local-controls/report.json`.
Actual native UI prerequisite results remain pending.


The first UI prerequisite run
[41968381572](https://github.com/exec-software/exec-nil-framework/actions/runs/41968381572)
completed at `7ad0f11` on both native hosts. Both have a logged-in on-console Aqua
session; read-only input/accessibility capability queries report available.
The owned window became visible and closed correctly, but its queued key did
not reach the view, so event feasibility remains unproven. The dependency probe
also stopped before linking because its constrained pkg-config directories
omitted Apple SDK metadata for libffi, zlib, bzip2 and expat. Neither observation
establishes a product runtime failure.

Independent replay retained 36 owned phases and 72 raw streams per host, with
all source/tool seals and cleanup verified. Reconciliation SHA is
`3e4105c0ec42a3d557c2af29f2c5ab979d9530a536186c81e527ec41589af05b`
under `macos-parity-20261105/ui-probe-run-41968381572/reconciliation.json`.
The narrow correction adds the selected SDK's explicit pkg-config directory and
seals those four metadata files without rebasing Homebrew paths. Cocoa now uses
its real application run loop, a six-second owned timer, its own activation and
a local event monitor. Initial active/running/key/first-responder state, app
dispatch and view delivery are distinct observations. The timer closes/stops
only this application; no direct send-to-view shortcut or global input is used.
Fifteen controls and independent review pass; correction evidence SHA is
`5eae3ceb3cdc0519afe66b855c19c42b4851bcf431f37789d10a50d3a4fa7ff0`.
Native re-execution remains pending.

## PostgreSQL numeric-address consistency

The private-provider SDK run at `2f5d695` exposed a real Darwin difference:
`inet_pton(AF_INET6)` accepts `%scope` suffixes, including interface names.
The PostgreSQL client contract requires plain numeric addresses, so the runtime
now rejects `%` before either platform parser runs. The existing diagnostics,
redaction and POSIX absolute Unix-socket support are unchanged. Four additional
negative cases cover named/empty scopes and scopes on loopback addresses.

The focused rebuilt Linux database runtime/client tests pass 2/2 with no skips.
A retained reproduction using Apple's parser source passes fourteen controls,
including five previously accepted scope forms rejected without parser calls.
That reproduction is host execution of Apple source, not native Darwin proof.
Linux evidence is `macos-parity-20261105/postgresql-numeric-host/linux-focused-report.json`,
SHA `cf330267eb62bd7558adbf00a5109242b67cc64f1c692135226bfa4a3424d444`. Independent review
passes; actual Windows and both native Mac regression checks remain pending.

## Installed debugger replay

The separate [replay workflow](../.github/workflows/macos-sdk-replay.yml) can run
the corrected debugger/checkpoint acceptance against an unchanged development
SDK artifact. It requires the completed source run, exact SDK and helper
revisions, the official ZIP digest/size, original complete CTest results and
original payload/pre-test tool hashes. Extraction, direct external prerequisites
and all after-state checks are explicit; an original failed test stays failed.
The original SDK source is never relabeled as the newer helper source.

Eighteen replay controls and seventeen debugger controls pass, with independent
review. Host-only extraction of the actual ARM artifact validates 813 evidence
files and all 261 SDK files (260 payload files plus manifest); it executes no
Darwin binaries and proves no current native prerequisite availability. Actual
native debugger replay remains pending. The workflow neither rebuilds nor
activates or publishes the SDK.

## ARM installed consumers and third UI prerequisite check

The `2f5d695` Apple Silicon candidate completes its installed standard-library
campaign with 1,184 reported passes: 888 retained executions independently
replayed and 296 object/link routes checked by the pinned driver without retained
child streams. Its private PostgreSQL provider passes in 167.78 seconds; installed
`nilstd` and `nillib` each pass six routes with owned server shutdown verified.
All payload, loader and original pre-test seals remain intact. The full source
inventory records 566 passes, two failures (incremental reuse and numeric host
validation) and three skips (UI and two lifetime bounds). The original debugger
helper fails before acceptance execution on the known schema mismatch.

Evidence is under `macos-parity-20261105/sdk-run-41966883934/arm64`.
Core reconciliation SHA is `4fbdffce936721ca320ef77436d2e19d4a95e41ed23106b819d0a96e39e69bd4`;
consumer reconciliation SHA is `7fe4755fa1cc3cb8376d399b1aeab8fa15d5b5209ab2802d793f0784143796c2`;
PostgreSQL reconciliation SHA is `c59ce0aed2dcd8b1a1729e19b2e8ec52220e3e7a619131002e36bf1c8bc122a7`.
The paired Intel run remains active.

The second UI probe, [41969185706](https://github.com/exec-software/exec-nil-framework/actions/runs/41969185706),
preserves another prerequisite failure: cooperative activation leaves the owned
application inactive, and the selected Apple SDK has no usable `libffi.pc` in the
probed directory. Neither is evidence of a working GTK provider. Reconciliation
SHA is `35f0737a50b59edaee1359e4c0207f5c5976337729b4ed231fa10c9e67b6da30`.

The third correction uses only pkgconf's observed Homebrew macOS 15 compatibility
directory and overrides its explicit `homebrew_sdkroot` variable with the selected
Apple SDK. Ordinary Homebrew library paths are not rebased. Own-application
activation follows GTK 4.18's Quartz behavior; active/key-window readiness,
actual application dispatch, view delivery and cleanup remain required. No
global input or permission changes are made. Sixteen controls and independent
reviews pass. Correction evidence SHA is
`0e2460f61ece3b14579ab6827164d579f9128cf7ea6d98c91d79360e83a051b3`;
native execution remains pending.

## Windows address regression and native UI dependency results

The private Windows build at `8d1f65d6a3150b2ad21e39fa834e6d0f5e903b8b`
passes both database runtime/client tests without skips, including eight bounded
IPv4/IPv6 stalls and socket closure. Eight owned phases retain sixteen raw
streams, exact source/tested executable hashes and verified cleanup. The
original clean checkout, accepted SDK, provider DLLs and all input seals remain
unchanged. Independent copied-evidence review passes. Reconciliation is
`macos-parity-20261105/windows-pg-8d1f65d/windows-reconciliation.json`, SHA
`368c47f677508d1d937d8b7956ac7314f53e9978ded3cc83d57021e6a23a4570`.

The third [native UI prerequisite run](https://github.com/exec-software/exec-nil-framework/actions/runs/41970256617)
at `3414ce5` passes actual dependency compilation, linking and loaded API checks
on both hosts, with header/runtime/pkg-config versions and loaded origins
matched. Each host retains 43 owned phases and 86 raw streams; all seals and
cleanup pass. Reconciliation SHA is
`155952d8b72b24d54f3178e85c70f2dfe942cb93b38fcc5afc8d71f1542d1782`.
The owned Cocoa window now activates and becomes key, but the view reports a
wrong target. That flag combines window and character checks; the precise cause
was not captured, so character normalization remains only a hypothesis.

The next narrow correction uses one consistent synthetic key (`n`, key code 45)
and records bounded metadata at queue, application dispatch and view delivery.
Only the retained synthetic event or its exact timestamp can contribute character
observations; unrelated input is not recorded. Readiness, exact delivery counts,
matching metadata, own close and cleanup are all still required. Eighteen
controls and independent review pass; correction evidence SHA is
`bfafd63196cc2a2238d81cb7c38bc2a52a77204018b1862a31f97aeb01a59796`.
This remains a prerequisite probe, not a GTK runtime port or parity declaration.
