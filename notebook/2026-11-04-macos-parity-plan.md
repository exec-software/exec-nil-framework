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
