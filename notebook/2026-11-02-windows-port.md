# Windows framework recovery and development SDK

## Status

Implemented and validated on Windows 11 x64 and Linux. The accepted Windows
development SDK was built from `0d8c6c21b20a16f16433a6f84ffe4ec1bdfb7bf8`.
This continues the interrupted MSVC port at `3dc9d17`; it is not a Windows
release or RC declaration.
The inherited working tree was preserved before synchronization to the VM.
No active SDK, Lich publication, or Anomalous checkout is changed by this work.

## Question

Can the same source, serialized NIL, object/link, and native routes behave
consistently on Windows, and can we package that result without mistaking
development evidence for release qualification?

## Decisions and implementation

- Keep output and input byte-exact on both platforms. Windows tools and generated
  executables use binary standard streams and UTF-8 manifests. Test raw bytes,
  Unicode arguments, literal carriage returns, empty arguments, and paths with
  spaces/metacharacters; do not let Python newline normalization hide a mismatch.
- Discover sibling `.exe` tools from the running executable, respecting explicit
  overrides and Windows PATH rules. Test installed-style discovery without
  compiler/interpreter overrides.
- Exercise the Windows process backend, not a platform skip: bounded parallel
  work, output caps, timeouts, process-tree cleanup, failure cancellation, and
  console interruption run through real native launchers/private consoles.
- Fingerprint the selected MSVC driver, helper files, loaded compiler modules,
  ordered library search paths, and recursive `/DEFAULTLIB` closure. Re-resolve
  after compilation. Unrecognized or incomplete toolchain inputs disable cache
  reuse rather than producing a false cache hit.
- Keep native linker descendants strict by default. The selected absolute MSVC
  driver may leave its adjacent `vctip.exe` telemetry uploader alive after a
  successful link. An explicit policy verifies the actual driver image and all
  remaining Job members, terminates only exact-path, Job-owned helpers through
  retained handles, and requires confirmed termination and an empty Job. Unknown
  children, failed drivers, and unconfirmed cleanup remain failures. Framework
  tools do not modify machine telemetry preferences or toolchain installations.
- Use Unicode Win32 environment APIs. An empty value remains present; only an
  explicit unset deletes it. Test non-ASCII names/values and enumeration.
- Preserve the AI-facing executable context on Windows as PE/COFF `.nyctx`
  sections, with read-only structural validation before decoding the record.
- Package into a unique Windows **development** candidate with tools at its root,
  `nillib` binary libraries, `nilstd` sources, and compiled documentation.
  `scripts/build-sdk.ps1` records a fresh source/build snapshot, runs the complete
  registered CTest suite, installs, packages, validates, and runs the SDK consumer
  route gate. It never activates or publishes the candidate. Failed candidates
  retain their evidence.

## Evidence

The reference VM uses Windows 11 x64, MSVC 19.44, LLVM 18.1.8, a single-config
Release/Ninja build, tests and experimental tools enabled. Full native testing
requires the Visual Studio developer environment, not just an executable PATH.

Focused Windows tests pass for tool discovery, parallel processes, niltest
process control, raw-byte Ny/Matrix routes, the MSVC input-closure unit test, and
the common system-runtime fixture. The incremental-build contract now also
passes: legitimate MSVC `/disallowlib` and `/ThrowingNew` directives had caused
the conservative dependency scanner to disable native reuse. Exact handling of
these directives preserves rejection of unknown toolchain behavior.

The final Linux regression run passed all executed tests (545 registered, 543
passed, no failures; optional display and PostgreSQL tests skipped). The MSVC
closure fixture separately passed 43 checks, including real captured linker
directive output. PE/COFF boundary cases run in `context.manifest` on both hosts.

The first complete Windows run registered 543 tests: 536 passed, six skipped,
and one failed because the staging fixture expected the old fallback-linker
diagnostic. That fixture now recognizes the selected-MSVC failure and still
checks the missing-library identity, unchanged caller output, and staging
cleanup. Follow-up checks also cover raw version bytes for every framework
tool, debugger LF/CRLF command parity, and identity-safe process cleanup.

Two test-fixture races were corrected without removing assertions: concurrent
Windows CRT append writes could lose event records, and cleanup revisited old
numeric PIDs without checking creation identity. Events now publish separately;
cleanup verifies identity through retained Windows handles or Linux pidfds.
PID reuse fits an intermittent driver exit, but its original exit status was
not recorded, so that causal explanation remains unproven.

A diagnostic Windows package installed all eleven tools, generated 66 HTML
documentation pages, strictly compiled 18 standard-library modules, and sealed
259 files. It is **not** a validated deliverable: native niltest SDK smoke hit
the linker's residual-descendant guard. Bounded job-member diagnostics were
added before cleanup without relaxing the guard. Clean-revision SDK validation
and its full CTest/consumer gate are the next acceptance boundary.

The recovered port was checkpointed and pushed as `058c32b`. Subsequent isolated
validation identified the residual process as MSVC's `vctip.exe`. The scoped
helper policy has Windows regression coverage for default-strict behavior,
exact-path cleanup evidence, wrong-directory namesakes, mixed unknown children,
nonzero leaders, timeouts, Unicode/spaced paths, and an identical helper image
outside the Job remaining untouched. The Linux suite also passes with this
policy (545 registered, 543 passed, two optional skips). Real installed-SDK
cleanup confirmation and final clean-revision packaging remain under validation;
synthetic process tests alone are not acceptance evidence for the SDK.

The real VM uploader remained unsignaled after `TerminateProcess`, even through
a further two-second diagnostic observation. Direct compiler execution outside
`niltest` reproduced this. A later empty Job count after `TerminateJobObject`
did **not** establish that the retained helper handle was signaled. The final
policy therefore requires both individual helper completion and an empty Job,
under the original bounded confirmation deadline. Temporary observation code
was removed; no timeout increase or accounting-only success was accepted.

With explicit user approval, optional Visual Studio Build Tools telemetry was
disabled in the Windows VM using Microsoft's documented VS2022 setting:
`HKLM\SOFTWARE\WOW6432Node\Microsoft\VSCommon\17.0\SQM\OptIn`, DWORD `1` to `0`.
The prior key was exported to
`C:\provision\msvc-telemetry-before-18003bab74714865a035b231dc022055.reg`.
There was no overriding Visual Studio SQM group policy. This is a VM provisioning
change, not an automatic behavior of the framework or SDK. Required Microsoft
diagnostics and unrelated Windows/host settings were not changed. See
[Microsoft's Build Tools opt-out documentation](https://learn.microsoft.com/en-us/visualstudio/ide/visual-studio-experience-improvement-program?view=vs-2022).

The registry opt-out alone did not prevent a fresh uploader launch. Under the
user's explicit instruction to disable compiler telemetry either way, the exact
selected toolchain's optional uploader was reversibly renamed from
`VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\vctip.exe` to
`vctip.exe.disabled-nil-20261102` in the same directory. Its valid Microsoft
signature and SHA-256
`D591358C6AEE4B673562CA3B3A15C250E612708394FE3B44A7A7EDA2470570A5`
were verified; the linker hash remained unchanged. Restoration details are in
`C:\provision\vctip-disabled-nil-20261102.json`. No compiler/runtime files were
deleted, no unknown process was allowed to escape, and no cleanup check was
weakened. This follows the same optional-uploader omission documented in
[Chromium's Windows toolchain packaging](https://chromium.googlesource.com/chromium/tools/depot_tools/+/refs/heads/main/win_toolchain/package_from_installed.py).
Toolchain repair or upgrades may restore the uploader; this VM-specific
provisioning state is part of the validation environment, not a claim about
unmodified installations or an automatic SDK installation step.

After the reversible uploader disable, 30 consecutive native `niltest` smoke
builds passed with exact JSON/output checks and no stderr. The final Linux run
again passed 543 tests with two optional skips (545 registered, 97.95 seconds).
The clean-revision Windows SDK pipeline was retained as the final acceptance
gate; the successful result is recorded below alongside the failed attempts.

The first clean SDK gate at `f173a3149e4e658fdc37522a13ab92714284f8f1`
registered 543 Windows tests: 536 passed, six skipped, and `nil.project-edition`
failed because the compiler's loaded-module graph differed between analyses.
The candidate and JUnit report were retained under
`build/windows-candidates/development-39b7fb49b4d84e85a17670a9bc24a0d8`.
Eighty direct analyses had identical 12-module lists, and 30 edition-test
repetitions under four bounded CPU workers passed, so the cause is not yet
established. `nilbuild` now reports up to eight added and removed module paths
when rejecting a changed graph; it still rejects publication and retains the
previous artifact. A synthetic graph-change regression verifies that contract
and stable subsequent reuse on Linux and Windows. No module normalization,
ignored dependency, relaxed cache check, or automatic retry was introduced.

The next clean gate at `93d843607775f36b222bd313eb64a21cda295a28`
passed the edition test, but exposed a timing assumption in the parallel-process
fixture (536 passed, six skipped, one failed; 407.63 seconds). Its preserved
`success-2` ledger recorded only one overlapping worker even though all five
workers completed with exact output and no backend error. Short 50/250 ms
sleeps cannot guarantee counted overlap when Windows byte-lock contention can
delay a worker for a one-second retry. The fixture now holds the first wave at
an explicit, bounded rendezvous after each worker enters the counter. It still
requires `maximum == jobs`, zero remaining active workers, exact output, and
all existing cancellation/descendant/output-limit checks. A deliberately
delayed second worker exercises startup skew beyond the old overlap window.
This changes the test, not the scheduler. Failed candidate
`development-1ee97fe987294feb9cbbb89046f2ce6c` remains preserved.
Focused checks passed on Linux (9.38 seconds) and Windows (8.80 seconds),
followed by ten Windows repetitions under four bounded CPU workers (all passed,
91.00 seconds total; workers stopped). Disposable negative controls confirmed
that a forced-serial runner fails the rendezvous and removing the rendezvous
reproduces the old incorrect maximum in the delayed-start scenario.

Reproducible checks:

```text
cmake --build <build> --parallel 4
ctest --test-dir <build> --output-on-failure --parallel 4 --output-junit <report>
powershell -File scripts/build-sdk.ps1 -BuildDirectory <Windows Release build>
```

## Final acceptance

The clean Windows candidate
`C:\src\exec-nil-framework\build\windows-candidates\development-ae8ea0b03e514d8daa807da75d7b1f91`
completed the entire `scripts/build-sdk.ps1` workflow at source revision
`0d8c6c21b20a16f16433a6f84ffe4ec1bdfb7bf8`:

- Windows CTest: 543 registered, 537 passed, six documented skips, zero failures
  (418.80 seconds). Both the edition and parallel-process tests passed.
- Linux CTest: 545 registered, 543 passed, two optional skips, zero failures
  (105.30 seconds), including the final diagnostic and rendezvous regressions.
- SDK contract and tool smokes passed: 11 root executables, 18 strictly checked
  logical standard-library modules, 66 HTML pages, and 259 sealed payload files
  plus the manifest. The root layout remains tools, `nillib`, `nilstd`, and `docs`.
- Installed consumer gate: all 1,184 executions passed (74 fixtures, both source
  and binary libraries, eight routes including standalone object assembly/linking).
  The gate rechecked every payload hash and the unchanged manifest on completion.

Windows skips remain `nil.ui-display`, `nilbuild.project-contract`,
`ny.native-managed-lifetime`, `ny.native-owned-lifetime`,
`ny.sdk-publish-concurrency`, and `nil.database-postgresql`. Linux skips remain
the optional display and PostgreSQL providers. A passing executed-test count
does not convert these into platform coverage.

The one earlier module-graph rejection has not been assigned a proven cause;
its failure evidence and strict added/removed-path diagnostic remain available.
The successful clean gate is development evidence, not a claim that the
intermittent observation was explained or that Windows is release-qualified.

The verified host handoff is
`/home/nathan/vm/nil-win11/artifacts/0d8c6c21b20a/nil-framework-windows-development-0d8c6c21b20a.zip`
(28,355,686 bytes, 260 archive entries), SHA-256
`5d26476dbc4e71c2bab46c1940415a97a9f6d243ab58cdcf3f061e6e24a9cee5`.
ZIP inventory, CRCs, every payload size/hash, the manifest, consumer identity,
and copied evidence were independently checked on the host without executing
Windows tools. `delivery.json`, the checksum sidecar, build snapshot, Windows
and Linux JUnit reports, consumer report, full `pipeline.log`, PowerShell
transcript, and `windows-dependencies.json` are beside the ZIP, not inside the
SDK root. PE imports for all 11 tools and the shared runtime reference only
Windows/system and Microsoft C/C++ runtime DLLs; this is import inventory, not
validation on a bare Windows installation. The registry opt-out remained zero,
the selected uploader remained reversibly disabled, and no owned build/test
processes remained running at handoff. No SDK activation or publication occurred.

This notebook's final evidence checkpoint is documentation-only and later than
the archive's pinned source revision; it does not re-label the existing package.

## Follow-up and boundaries at the first candidate

Windows remains a development target: POSIX-only lifetime/project checks,
optional display and
PostgreSQL providers, and atomic publication constraints must not be concealed
by a green executed-test count. The established RC/release promotion profile
remains Linux-only.

The matching x64 Microsoft Visual C++ runtime remains a host prerequisite, and
native compilation additionally requires MSVC Build Tools and the Windows SDK.
These dependencies are not redistributed or certified for a bare Windows host
by this package. GitHub also reported an existing moderate Dependabot alert
(number 7) during the pushes; that separate dependency issue was not audited here.

Windows staging preserves exclusive creation but inherits the parent ACL; it
does not promise POSIX-style owner-only isolation in shared output/TEMP trees.
The compiler/linker already had this limitation before the recovered port.
Explicit owner-DACL creation and cross-user ACL regression tests remain a
separate hardening task; use trusted user-controlled directories meanwhile.

## Mainline parity follow-up

The next pass closes behavioral/coverage gaps rather than treating skipped
Windows checks as equivalent to Linux coverage. The original candidate above
is unchanged; the following changes require a new source-pinned candidate.

- The common project contract now runs on Windows: mixed-language checking,
  VM/native build/run/test, manifest and path policies, atomic outputs,
  symlinks/hardlinks, cancellation, and forwarded stdin/stdout/stderr/exit
  status. Windows' resident parent and child identities are tested explicitly.
  POSIX exec identity, controlling-terminal input, and FIFO rejection remain a
  separately registered UNIX-only contract, not a broad Windows test skip.
- Both native lifetime tests retain their original workloads (>625 MiB and
  >2 GiB cumulative allocation) and 96 MiB limits. Windows uses a test-only
  suspended-child Job launcher: the process committed-memory limit is attached
  before execution. This is not the same metric as Linux's `RLIMIT_AS`, nor RSS.
  Uncapped/capped/uncapped allocation controls prove enforcement; argument,
  timeout, bounded-output, and lingering-descendant controls also execute. The
  helper is not installed in the SDK.
- SDK directory-link switching uses Windows `FileRenameInfoEx` POSIX replacement
  semantics, with no delete-then-create fallback. Disposable tests cover 251
  swaps with concurrent readers, a held old-generation file, Unicode/spaces,
  unsupported-operation cleanup, real sharing violations, and existing
  publication/rollback races. No active SDK was touched. A metadata-only handle
  did not establish the intended sharing violation; the final negative control
  uses actual read access and exclusive sharing.
- Aperture's full memory contract now runs on Windows. It exposed a genuine
  native-source bug: the linker wrote `program.exe`, but Aperture launched the
  extensionless path. The launcher now uses the actual filename. The portable
  contract retains source identity, allocation/ownership/GC/parallel accounting,
  input and failure checks, and 101 assertions; a real nonprofiling Python child
  replaces the `/bin/true`-only control. Both hosts pass it.
- Previously hidden Windows symlink exclusions in context snapshot inspection
  and memory-report capture are removed. These tests now require the fixture,
  reject the operation, and verify that link and target survive. FIFO checks
  remain POSIX-only.
- Private staging now supplies a protected effective-user DACL at directory
  creation and verifies it before writing sensitive descendants. Unsupported
  security semantics fail closed. Compiler/linker staged files are checked via
  non-reparse handles; copied interpreter stubs are secured before bundle bytes
  are appended. The new contract checks public positive controls versus
  restricted-token read/write/create denial, nested inheritance, collisions,
  copied/read-only stubs, reparse rejection, and held-file delete exclusion.
  This is real restricted-token I/O coverage, not a separate-account test.

The first combined Windows gate rejected normal files whose owner came from
`TokenOwner` (the elevated token's default group), rather than `TokenUser`.
The correction accepts only the effective user's/default creator-owner SID in
an already-exclusive private staging path, installs the protected user DACL,
then obtains `WRITE_OWNER` through `ReOpenFile` on the retained object when
needed and assigns the effective user. Final verification still requires that
user as owner and the exact protected DACL. No privilege is enabled. A read-only
default-owner regression covers this case; metadata-only handles are not used
to claim delete-sharing exclusion. Original failing evidence is preserved in
`C:\provision\codex-parity-default-owner-rejection.{log,xml}`.

The first complete Linux parity gate passed 545/547 registered tests, with only
the optional display/PostgreSQL tests skipped, in 101.44 seconds. The corrected
combined Windows focused gate passed all 13 selected tests. A final source
checkpoint and clean Windows SDK workflow follow; these focused results alone
do not establish a release qualification.

Remaining platform gaps are explicit: the display provider loads GTK only on
Linux; PostgreSQL is compiled out on Windows (not merely missing a DLL); and
`nilagent`'s Windows hot-reload connection/discovery are still stubs despite an
older Named Pipes comment. Linux ELF toolchain closure and POSIX terminal tests
are platform-specific; Windows has its own toolchain collector and process
contracts. Windows release assembly, PE/CRT redistribution/minimum-OS policy,
and bare-host validation still require their own reviewed qualification. The
Linux-only RC profile is unchanged. The earlier intermittent module-set
observation is still not explained; strict graph checks remain in place.

Secured Windows bundle/native outputs retain their owner-only descriptor when
published. Cross-user sharing is an explicit later permission change. Trusted
parent ancestry remains necessary on both hosts; these controls do not protect
against administrators or an ancestor owner replacing paths. The documentation
now describes this boundary rather than the old inherited-ACL limitation.

Relevant platform contracts:
[directory security at creation](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createdirectoryw),
[default token owner](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-token_owner),
[reopening a retained object](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-reopenfile),
and [restricted-token access checks](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-createrestrictedtoken).

### Exact-source validation and evidence-parser correction

Parity source checkpoint `ae42a34f6b3bc240781517de8ccf49deda9cdb9e` was
committed/pushed and synchronized to the clean VM without resetting either
worktree. Its final Linux gate passed 545/547 tests in 104.03 seconds; Windows
passed 544/546 in 420.62 seconds. Each had only display/PostgreSQL skips. An
independent JUnit comparison found exactly two Linux-only contracts (ELF
toolchain inputs and POSIX process semantics) and one Windows-only contract
(the Job memory limiter), with no failure/error elements or duplicate names.
All new Windows contracts executed rather than skipping. The Windows JUnit
inventory matched every registered-test entry in the captured build evidence.

That evidence review also caught a metadata bug: `cmake_value()` stopped a
quoted compiler path at the closing parenthesis in `Program Files (x86)`.
This did not change the selected compiler or execution results, but it made
the recorded compiler identity incomplete. Quoted values now terminate at
their closing quote, while unquoted scalars retain their original trimming.
Regressions cover the real MSVC path, quoted parentheses/semicolons/whitespace,
unquoted scalars, vendor-version strings, malformed assignments, and complete
Windows build-configuration extraction. All 45 qualification unit tests pass.
The final delivery is regenerated from the corrected clean source rather than
rewriting the already-sealed candidate's evidence.

### Final parity SDK acceptance

The corrected clean source revision is
`3ba1915c3b923fd52800805c8c7809f7249e7080`. Both source commits are pushed. Its
fresh Windows candidate
`C:\src\exec-nil-framework\build\windows-candidates\development-dd546fb3032041e19366e121f0b41408`
completed the unmodified full development-SDK workflow:

- Linux: 547 registered, 545 passed, two optional skips, no failures
  (115.11 seconds).
- Windows: 546 registered, 544 passed, two documented display/PostgreSQL skips,
  no failures (420.62 seconds). All newly ported contracts ran. The focused
  Windows qualification unit invocation ran 43 checks and skipped the two
  Bash-builder-only checks; all new compiler-path regressions executed.
- Installed SDK consumers: all 1,184 executions passed, covering 74 fixtures,
  both standard-library forms, and eight execution routes. The gate rechecked
  every sealed payload and the unchanged manifest afterward.
- SDK layout: 11 root EXEs, `nillib`, `nilstd`, and 66 HTML documentation pages;
  18 logical standard-library modules and 259 payload files plus the manifest.
- The build evidence records the complete selected MSVC compiler path, not a
  truncated `(x86)` prefix, and clean source identity matches the package.

The host handoff is
`/home/nathan/vm/nil-win11/artifacts/3ba1915c3b92/nil-framework-windows-development-3ba1915c3b92.zip`
(28,372,902 bytes, 260 entries), SHA-256
`964b91b917ecc21c06d070261e2314f95df4c86a773034649f37cde683959ddf`.
Archive CRC/inventory, every payload size/hash, manifest/consumer binding,
copied evidence hashes, full compiler path, and the checksum sidecar were
verified on the host without executing Windows binaries. Delivery metadata,
Windows/Linux JUnit reports, consumer report, full pipeline log, transcript,
and 12-file PE dependency inventory are alongside the archive. Import inventory
does not certify a bare Windows installation or redistribute prerequisites.

The superseded `development-26880430952f412b876a20c1bac4393e` candidate's
consumer parent was deliberately stopped after the metadata defect was found;
its observed child finished, and its logs/scratch files and failed marker were
preserved. It is not a delivered SDK. The previously accepted `0d8c6c21b20a`
archive also remains untouched. No resets or source changes were discarded.

At final verification the VM worktree was clean, no owned compiler/build/test
process remained, optional telemetry OptIn was still zero, and the accepted
candidate had no failed marker. No active SDK, Lich release, or Anomalous project
was changed. The known provider/hot-reload/platform-qualification gaps above
remain; this is a validated Windows development SDK, not a Windows release
declaration. This subsequent notebook checkpoint does not relabel the archive's
pinned source revision.

## Installed-SDK debugger and editor follow-up

The next parity slice exercises consumers rather than treating compiled tools
as complete workflows. The debugger consumer harness previously depended on
unpublished fixtures and POSIX pipe/process APIs. Its small session, pause and
standard-library fixtures are now published. Both textual and binary sessions
exercise conditional source breakpoints, frames/locals, shadowing, stepping,
restart, output, pause/termination, real mesh/scene applications, and stepping
into the installed `nilstd` math source. Native output is compared with the
interpreter; native attach and unmapped source debugging remain explicit
rejections, not claimed features.

The portable harness preserves the selected Windows developer environment,
uses actual `.exe` outputs and file identity, and reads pipes with deadlines.
Windows assigns a trusted launch gate to a private Job before releasing it;
the retained Job owns cleanup. POSIX retains the unreaped group leader until
signal-and-reap cleanup, serialized against background output-limit callbacks.
Output/transcript budgets, blocked writes, setup failures and report/source
path protections are tested. SDK payloads, origin anchors and consumer sources
are checked before and after sessions. A test-only editor launcher reuses the
existing toolchain process runner and is excluded from SDK installation.

The accepted Windows SDK remains pinned to `3ba1915c3b92`, with its manifest
SHA-256 independently anchored from the verified archive:
`e53b66b4c9c05bb15c780c53f71e2757c1f27464fc716f40d16ba71b46162571`.
All 146 installed-SDK debugger assertions pass on Windows and Linux. Windows
passes all 19 harness regressions; Linux runs 17 and explicitly skips the two
Windows Job-only controls. The Linux consumer uses a separate temporary,
self-anchored development SDK whose manifest records dirty source and makes no
release-qualification claim. The first full Linux gate with the new registered
harness has 548 tests: 546 passed, display/PostgreSQL skipped, no failures.

The debugger artifact-loading contract now requires its real Windows symlink
fixture and checks preservation of both link and target, rather than silently
omitting the assertion. The STL README was reconciled with checked database
providers, wide Windows file APIs and shell-free Windows linking. Language
path conversion still depends on the process code page; current MSVC binaries
request UTF-8. Explicit malformed-UTF-8 and unmanifested-consumer behavior need
separate follow-up, not a claim that the current SDK's Unicode paths are broken.

For editor validation, official portable Node 24.22.2 and VS Code 1.144.0
archives were checksum-verified and extracted only under `C:\provision\tools`.
No machine PATH, file association, login policy, or existing editor profile was
changed. The isolated profile disables telemetry/updates and GPU use. Its first
real Windows session passed nine language-service checks but failed to receive
the rejected compiler task's expected Problems entry. That evidence is retained;
the editor gate is not yet accepted at this checkpoint. Six pre-existing
Windows-only editor fixture omissions are now explicitly reported and remain
separate from debugger protocol parity. Further editor results follow below.

### Actual-console UTF-8 correction

The editor failure was traced to a real core boundary: the task argv contained
`café 中文`, but console output decoded the UTF-8 bytes as CP437 and the Problems
matcher created a diagnostic for `caf├⌐ Σ╕¡µûç`. Pipe-based analysis retained the
correct URI. Both shell and direct-process tasks reproduced it, so direct argv
execution alone is not the encoding fix.

The common stream initializer now selects UTF-8 for input/output directions
actually backed by a Windows console, using the CRT descriptor's handle and
`GetConsoleMode`. Binary pipe/file behavior is unchanged; NUL is not mistaken
for a console. This changes the attached console's shared session code page,
not Windows registry, machine locale, or defaults. Every framework tool and the
native runtime already call this common initializer.

The new Windows-only contract allocates its own private console after detaching
only the test process. A CP437 negative control demonstrates the original bad
decoding, then exact Unicode screen cells and keyboard-to-UTF-8 input must pass.
Mixed console/pipe directions, all 256 redirected byte values, CR/LF/Ctrl-Z/NUL,
EOF, and NUL-device controls also execute. An initial CJK assertion clipped a
two-cell glyph by reading one cell; the corrected oracle requests both cells
and still requires exactly the intended Unicode code point. Both the original
failure and codepoint diagnostics are retained. The corrected Windows contract
passes; Linux's full post-header gate again passes 546/548 with only the two
documented optional-provider skips (99.78 seconds).

Running the debugger harness alongside the other focused tests also exposed a
cleanup race after Job accounting reached zero: a terminated descendant could
still hold its working directory. That failure is retained and is being fixed
by waiting for verified, retained process handles as well as an empty Job;
neither ignored cleanup errors nor a fixed sleep is an acceptance criterion.

### Verified Job cleanup and portable editor fixtures

The debugger harness now retains query/synchronize-only handles for members of
its exact Job, verifies membership on those handles and records creation times.
Termination still targets only the retained Job, never reopened numeric PIDs.
Cleanup requires both zero active Job members and signaled retained handles
under one five-second deadline. Cumulative Job population changes during the
snapshot/termination boundary reject acceptance; snapshot failures still stop
the owned Job and report the verification failure. The full finish/drain/close
sequence is serialized against output-budget callbacks.

All 22 lifecycle regressions pass on Windows. Twenty further repetitions of
the orphan teardown test exercise 240 immediate working-directory and
exclusive-file deletions without sleeps or ignored cleanup errors. Linux runs
17 cases and explicitly skips five Windows-only controls. The installed-SDK
Linux and Windows debugger matrices again pass all 146 checks after this change.

Editor source compilation and interpreter tasks now use literal process argv,
without a command shell. The portable real-host launcher binds evidence to a
unique run and workspace, preserves failed profiles/reports, and requires the
test-only bounded native runner on Windows. It does not accept stale reports
or a passing report from a timed-out/nonzero host. The Unicode workspace exposed
several test assumptions after the console fix: drive-letter spelling is now
checked by file identity, and an untitled document's native EOL is asserted
before requiring exact rename/undo preservation. These are fixture corrections,
not relaxed output assertions.

Remaining editor gaps are explicit: four controlled-compiler cancellation/retry
fixtures and two semantic alias fixtures are not ported to Windows. The
workspace SDK-build command still selects the Bash workflow; native Windows
SDK assembly uses `scripts/build-sdk.ps1` separately. A local Linux editor-host
attempt produced no identity-bound report and is not counted as a pass; this
session has no configured display. Unit/CTest evidence is separate from a real
editor-host result. Final Windows editor and freshly sealed SDK results follow.

The Windows mixed build-tree editor preflight passed all 81 enabled real-host
checks, with the six exclusions recorded separately (run
`fb9621ba-1956-4e11-a0db-a828ede799c9`). Tools came from the rebuilt tree and
library/source support from the older accepted SDK, so this is not acceptance
of a newly installed SDK. Final extension units passed 147/147 on Linux and
146 with one POSIX FIFO skip on Windows; both production bundles built. All
owned editor processes exited. The next gate builds a clean, revision-pinned
Windows development SDK and repeats consumer/editor checks against that exact
sealed payload, without replacing or publishing any active SDK.

### Clean debugger/editor SDK delivery

The stock Windows development pipeline completed from clean source
`fdaf9759974e72fe762760e0efa5dda98b419075`. Candidate
`development-7a468a0483a041ccbde355b5bf74f88f` passed 546 of 548 CTest cases
in 636.55 seconds; only `nil.ui-display` and `nil.database-postgresql` were
skipped. The console UTF-8 contract and all 22 debugger lifecycle regressions
passed under the parallel suite. The final Linux suite also passed 546/548,
with those same two optional-provider skips, in 100.56 seconds.

All 1,184 installed-consumer executions passed: 74 fixtures, both source and
binary library forms, and eight routes including standalone assembler/linker
execution. The SDK has 11 executables at its root, 18 strictly compiled logical
library modules, 66 compiled documentation pages, and 259 sealed payload files.
Neither test-only C++ helper is installed. Its manifest SHA-256 is
`4031831d1998cf759cacfdbd687a09b7f18ca1a3915dd7d272d1c70c114026eb`.

Against this exact SDK, the debugger consumer passed all 146 assertions with
its source fixtures, external origin anchor and all payload hashes unchanged.
The real Windows editor passed all 81 enabled checks, recording the same six
fixture exclusions (run `e35a7202-557a-4d58-bd8c-175b7dfb6622`). Separate
observation evidence binds seven configured tools, 31 actual tasks across eight
task kinds, and both resolved debugger executables to this candidate. The
observation wrapper invokes the committed suite unchanged; evidence records
both the original and actual launcher arguments. The strict owned-process
helper exited zero. Source, bundle, SDK manifest and payloads matched before
and after. These final checks ran in separate workspaces concurrently with
the read-only installed-consumer matrix.

The delivered archive is under
`/home/nathan/vm/nil-win11/artifacts/fdaf9759974e/`:

- `nil-framework-windows-development-fdaf9759974e.zip`, 28,374,945 bytes;
- SHA-256 `9e9c26eb90d9bfc837a7b3d4d95fdbac627311316054e3cc8fdda0ef141f7363`.

The host verifier checks every ZIP payload against the seal, the exact root
layout, clean source identity, build-evidence bindings, the registered CTest
inventory and named skips, and all 1,184 consumer results. Delivery metadata,
dependency inventory, pipeline log, Windows/Linux JUnit reports, and separate
`editor/` and `debugger/` evidence are alongside it. The debugger origin file
was created before archival and deliberately remains unchanged; the final ZIP
manifest reconciles to its independently copied manifest anchor. This is a
development artifact, not a bare-host or Windows release qualification.

The matching editor package is
`/home/nathan/vm/nil-win11/artifacts/debugger-editor-20261102/nil-framework-editor-fdaf9759974e-pinned.vsix`
(210,464 bytes), SHA-256
`5ac19219399437289ab13a46b541196198331da4f6512f020ef4d74f4723d4da`.
Its 17-file inventory excludes tests and tool binaries. Its production bundle
matches the actual Windows editor bundle byte-for-byte. The first VSIX's
default repository-root link rewriting lost the extension's `vscode/` base;
it is retained as superseded. The delivered replacement uses explicit
`--baseContentUrl` and `--baseImagesUrl` values ending in this commit's
`/vscode`, so README documentation/license links resolve to the correct pinned
files. Neither VSIX was published or installed into a user editor profile.

The guest overlay was backed up before a verified-index fast-forward, with no
reset or discarded changes. At the delivery boundary, guest source was clean,
scoped framework/editor processes were idle, optional Visual Studio telemetry
OptIn remained zero, and no optional uploader was running. Existing accepted
SDKs, active SDK selection, Lich and Anomalous were not changed. A later
notebook-only checkpoint does not relabel the artifact's source revision.

### Next Windows STL contract slice

Read-only follow-up found a coverage boundary, not a demonstrated shipped-SDK
filename corruption: `secure_file_io.cpp` converts language string paths using
`MultiByteToWideChar(CP_ACP, 0)` before calling wide filesystem APIs. Empty/NUL
paths are rejected, but strict malformed-UTF-8 rejection is not implemented.
MSVC tools/generated executables carry UTF-8 manifests; external unmanifested
runtime hosts remain a distinct, untested boundary. The console code-page fix
does not change this path conversion. Existing Unicode tests cover arguments,
output and compiler/link-input paths; secure byte-file fixtures still use
ASCII filenames. The library API comment also still describes narrow host
calls imprecisely, despite the wide calls after conversion.

The next useful tests should have an independent wide-path host create exact
Latin/CJK/astral/decomposed filenames and payloads, then check language reads,
atomic replacement and no-clobber across supported routes and library forms.
The host must verify exact filename/codepoint inventory, payload bytes,
sentinel preservation and absence of staging or wrongly decoded siblings.
Choose the UTF-8 path contract explicitly before enforcing malformed-byte
rejections. Include separate manifested/unmanifested host probes recording
`GetACP`, without changing VM locale. A write/read round trip performed only
inside Ny cannot detect both operations making the same wrong conversion.
Interpreter path/list/stream operations need a separate follow-up; legacy
unwired `nilstd/native/io/file.windows.c` declarations are not evidence about
the active runtime. Six editor fixture ports, native PowerShell SDK-command
integration, optional providers and platform release qualification remain
separate work.

### Strict UTF-8 byte-file boundary and editor fixture parity

The follow-up chose an explicit contract for `read_regular_bytes` and
`write_bytes_atomic`: non-empty, NUL-free, strict UTF-8 filenames, with no
Unicode normalization. File payloads remain arbitrary bytes. Additive shared
helpers now validate scalar encodings on both hosts and use explicit
`CP_UTF8`/`MB_ERR_INVALID_CHARS` conversion before wide Windows filesystem
operations. Only the interpreter and native language providers switch to
these helpers; internal C++ host-path overloads preserve their prior behavior.
No secure staging, namespace containment, ACL, or durability guarantee was
broadened. Library and platform documentation now distinguish these scopes.

Baseline evidence was captured before changing the Windows runtime. The
previous accepted `fdaf975` SDK passed all 16 **valid** Unicode language routes.
The independent native probe then demonstrated two separate gaps: an
unmanifested embedding host (`GetACP=1252`) could not read the host-created
exact Unicode filename; a manifested host (`GetACP=65001`) accepted a malformed
UTF-8 filename. The old Linux runtime also accepted malformed raw-byte paths.
After the fix, both Windows embedding modes pass the same ABI probe, including
eight valid relative/absolute cases and sixteen malformed-path cases. No
machine locale or process manifest policy was changed to make this pass.

The new language/host oracle covers Latin, CJK, astral and decomposed spellings,
all 256 payload bytes, exact independent namespace inventory, no-clobber
sentinels, replacement and staging leftovers. Six malformed sequences are
constructed from byte-indexed slices of valid Ny literals. All read/create/
replace paths must return the exact redacted invalid-path error. Its 16
executions cover source and binary STL through text/binary interpreter,
bundles, assembler/linker, JIT and AOT. Build-tree CTest creates a private
18-module binary STL, copies rather than consumes the caller's native runtime,
and verifies the original hash. Failed runs retain their unique directory and
print its location; twelve independent oracle/retention/summary unit tests
cover the harness itself.

Initial Linux full regression: 551 tests, 549 passed and the existing UI-display
and PostgreSQL environment skips, 106.59 seconds. Initial Windows focused
runtime/route checks passed; SDK preflight initially failed because this VM's
effective PowerShell policy disallowed script files. The user explicitly
approved **process-scoped RemoteSigned only** for these build/test processes.
All four preflight tests (eleven negative prerequisite invocations) then
passed. Saved machine/user policy remains unchanged; neither the extension
nor repository script changes or bypasses execution policy.

Windows SDK tasks now use literal `ProcessExecution` PowerShell `-File`
arguments and the configured build directory. They require a clean checkout,
configured Release build, and inherited x64 Visual Studio developer tools;
missing prerequisites fail before candidate creation. POSIX keeps its existing
build-script contract. Editor host checks inspect the task descriptor without
executing a workspace SDK build. Full Windows regression and editor acceptance
evidence are recorded below after completion; this entry alone is not a new
SDK delivery or release qualification.

The pre-checkpoint Windows regression completed 552 tests (550 passed, the
same two environment skips) in 574.57 seconds. It deliberately excluded only
`nil.windows-sdk-preflight` while process-policy approval was pending; that
test's four cases subsequently passed separately under the approved scope.
There are 553 registered Windows tests and 551 Linux tests. The final clean
SDK pipeline must include the full Windows inventory, not reuse this split
development run as a packaged qualification report.

The six editor fixture ports preserve the original assertions. Four fake
compiler programs use a test-only native pipe adapter on Windows; semantic
aliases use real directory junctions with canonical-path/file-identity proof,
rather than duplicate files or silently skipped symlink checks. The adapter
uses a creation-time private Job and an exact standard-handle allow-list.
Cleanup retains verified member process handles and checks total lifetime
coverage as well as Job emptiness; unaccounted historical children fail closed.
Its initial `CREATE_NO_WINDOW` variant intermittently observed a second
unretained lifetime even for `process.exit(19)`. Bounded Job-local diagnostics
confirmed one Node process before resume and two total lifetimes after exit;
the additional process image was not observed, so it is not identified here.
The pipe-only `DETACHED_PROCESS` variant instead showed exactly one lifetime
and correct exit 19 in all three controls. The complete launcher contract then
passed, including binary pipes/Unicode argv, timeout, abort, output budget,
historical-child refusal, and twenty immediate descendant-CWD release controls
with a live-resource positive control before each termination.

The Linux real-editor run passed all 88 checks with zero exclusions (run
`a442c882-a1da-4d74-892e-bbde05737b16`). This is build-tree regression evidence,
not a fresh installed-SDK result: its source-backed support SDK is an older
dirty development fixture. Windows unit tests passed 151 of 152, with the
existing POSIX FIFO-only check explicitly skipped. Baseline and development
regression evidence is retained at
`/home/nathan/vm/nil-win11/artifacts/windows-stl-editor-20261102.85Ew98/`.

Final build-tree editor checks pass **88/88 with zero exclusions on both
hosts**: Windows run `830b8f6e-2727-464c-9a9b-744d6fd3cd09`, Linux run
`7bca4f4c-695d-45c7-8f10-5f96dd26f54d`. The first Windows run stopped at the
completion fixture, which echoed a lowercase-drive request path while the
editor's native canonical identity used uppercase `C:`. A direct byte-exact
fixture diagnostic confirmed `fs.realpathSync` preserved `c:` whereas
`fs.realpathSync.native` returned `C:`. Completion/signature fixture responses
now model the real compiler's canonical source identities while preserving
captured text. Product schema/identity checks and timeouts were not relaxed.
The isolated editor launcher also now writes telemetry/update settings to its
own `profile/User/settings.json` before launch: those application-scoped
settings were ineffective in workspace settings alone. No user editor profile
was modified. Final units: Linux 154 passed; Windows 153 passed with only the
POSIX FIFO test skipped. The final five Windows focused CTests all pass,
including the approved-policy SDK preflight and the twelve-test host oracle;
the retained strict Windows JSON report records all 16 routes and confirmed
private-directory cleanup. SDK construction/installed acceptance is the next
separate gate; previously accepted artifacts remain untouched.

### Sealed UTF-8/editor-parity SDK delivery

Implementation checkpoint `71a9338ec3a7edb5c4627bf9a489b057fc3d2fc5` is pushed.
The clean Windows checkout completed the normal development SDK pipeline:
553 registered CTests, 551 passed, only `nil.ui-display` and
`nil.database-postgresql` skipped, zero failures (530.16 seconds), followed by
all **1,184 installed-consumer executions** against source and binary STL.
The full JUnit report, rather than the earlier split development run, is bound
into this SDK's manifest. Process-scoped `RemoteSigned` was used only under the
user's explicit approval; a fresh shell still reports effective `Restricted`
and all saved policy scopes remain `Undefined`.

The fresh candidate is
`C:\src\exec-nil-framework\build\windows-candidates\development-c01605b9dc1d439e819542c17314eaad\sdk`.
It contains eleven root executables, eighteen binary standard-library modules,
the `nilstd` sources, native static/shared runtimes in `nillib`, and 66 HTML
documentation pages. Neither test-only editor helper is installed. There are
259 sealed payload files plus `manifest.json`; the manifest SHA-256 is
`d4e0ef1837bec3fc9081c30ff5d25855832bf86fcdc513a12ca5dcf3dd487881`.

Separate installed-SDK acceptance passed:

- Unicode byte-file contract: all 16 routes, supplied source/binary libraries,
  34 successful commands, exact independently checked filename/content
  namespaces, unchanged before/after origin snapshots, and private-directory
  cleanup. The final evidence prefix is `unicode16-71a9338ec3a7-v2`.
- Debugger consumer: all 146 assertions, with thirteen source/helper files,
  the external origin anchor, and the entire SDK unchanged.
- Real Windows editor: all 88 checks, zero exclusions, run
  `dc6a528e-3916-4db2-b857-db1a1f498752`. Observation evidence binds seven
  configured tools, 31 actual tasks across eight task kinds, and both stopped
  debugger sessions to this exact SDK. The owned host runner exited zero.

These checks used private workspaces alongside the read-only consumer matrix.
The first temporary editor/Unicode CMD wrappers hit log-file sharing errors
while reopening their vcvars log. The editor gate did not launch, and the
Unicode run lacked its before-origin snapshot; neither was accepted as
qualification. Their evidence remains preserved. Replacement wrappers use
separate stage logs and required-artifact checks. No SDK/source change or
relaxed assertion was needed. A separate PowerShell evidence-copy attempt
needed explicit UTF-8 JSON decoding to preserve the Unicode workspace path;
the successful copied settings match the editor gate's original hash.

The archive and evidence are retained at
`/home/nathan/vm/nil-win11/artifacts/71a9338ec3a7/`:

- `nil-framework-windows-development-71a9338ec3a7.zip`, 28,381,254 bytes;
- archive SHA-256
  `872cf43be600e25b09a35c9cc161e341dd8b9887a48afdf253e886705dcd1447`;
- `editor/nil-framework-editor-71a9338ec3a7.vsix`, 211,370 bytes, SHA-256
  `b146c9c3b708d4e3fa1a093f8500e87b19037e303cfd482987b4c51122379f57`.

The Linux host independently verified every archive payload, exact SDK layout,
clean revision, build/runtime bindings, full registered CTest inventory and
consumer evidence. Supplemental verification reconciles the externally copied
manifest with the Unicode/debugger/editor reports, actual command paths and
arguments, exact Unicode namespaces, copied libraries, and source hashes. The
supplemental binders passed 31 and 12 focused tests respectively, with separate
peer review. The VSIX has seventeen files, no test helpers or native tools,
correct commit-pinned relative README links, and a production bundle identical
to the actual editor-tested bundle:
`061c763ce82c767ccb0f8eb27e47aecc1ddfceef145b1cc4453d923c2965f556`.
The complete verifier outputs and reproduction helpers are in `verification/`.

At the delivery boundary, source was clean, scoped framework/editor processes
were idle, Visual Studio telemetry OptIn remained zero, and the optional
uploader remained disabled and absent from the process list. Older accepted
SDK/VSIX archive hashes remain unchanged. No active SDK selection, user editor
profile, Anomalous files, Lich activation or publication was changed. This is
a Windows **development** SDK, not Windows release/bare-host qualification.
The notebook-only follow-up commit does not relabel its source revision.

### Next STL slice after this delivery

Compatibility whole-file operations, File streams and path helpers have
confirmed interpreter coverage but no native provider; directory operations
also lack an active, complete public API. Prioritize a small cross-route
contract for these surfaces, with pure-Ny lexical path helpers where practical.
Separately, static inspection identified untested filename/path risks:
ordinary file APIs still use narrow Windows paths, and host filesystem parsing
may contradict the documented cross-platform separator behavior. Unicode,
embedded-NUL, backslash, drive-root and UNC behavior need focused tests before
being called demonstrated bugs. No implementation was started during this
read-only follow-up audit. Optional providers and platform release
qualification remain separate work.

### Execute Transfer Protocol reuse decision

**Status:** direction accepted by the user; source reconnaissance complete;
NIL wire migration not implemented or qualified. The Windows parity repairs
remain independent work and are not evidence of ETP interoperability.

**Question:** can the Execute format developed for Liz replace the divergent
experimental interpreter-control messages instead of growing another wire
format?

**Evidence:** the checked-out `exec-liz` revision
`af66c19eaafd6d3d5f3729b415812066cdb64668` contains ETP in
`legacy/src/server/etp.h`, `etp_server.cpp`, `etp_client.py`, and
`legacy/dashboard/lib/etp.js`. Those four files had no local changes during
inspection. Its frame is a four-byte little-endian **payload-only** length,
one-byte message type, then CBOR payload; total frame size is `5 + length`.
The C++, Python and JavaScript implementations agree on this boundary. The
header SHA-256 is
`c69e01f49b7834f4007054e330f1d0e95d56880f05da269bf9d51d597e127300`.
The existing message family covers sessions, inference streaming, reservoir
state transfer, metrics, errors and learning. Those application meanings are
not automatically equivalent to NIL VM state, execution control or reload.
The legacy server identifies this implementation as `ETP/0.1-draft`, not a
finalized general-purpose RPC specification.
The header references `Research/Chaos/Execute Transfer Protocol.md`; that
standalone specification has not yet been located in the inspected checkout
and project notebooks. Use the implementations as observed evidence, not a
claim that the entire original specification has been recovered.

The inspected current `exec-beholdd` server, revision
`caeb6e8fa301ec37937816a6d42c51c0b5a586cc`, exposes HTTP/JSON and SSE through
`crates/beholdd-server/src/lib.rs` and `protocol.rs`. No ETP implementation was
found in the inspected current Beholdd or earlier `exec-bog` source. This is
not a claim about every historical branch or deployment. No inference server,
model, GPU workload or Liz experiment was started or changed.

**Decision:** adopt ETP framing/CBOR as the intended common envelope for the
NIL control channel. Define an explicitly versioned NIL control profile and
capability negotiation rather than reusing Liz inference opcodes with
different meanings. Preserve request/response correlation, structured errors,
and room for typed events and binary payloads. Reuse the existing local Unix
socket / Windows named-pipe transports; adopting ETP does not require a TCP
listener or an inference-engine dependency. Keep debugger DAP and editor LSP
contracts unchanged.

Keep the repaired existing JSON client interoperable with the current
interpreter while the new profile is implemented and tested. Do not silently
change its unsolicited JSON handshake to binary framing. Choose explicit
protocol selection or separately versioned endpoints before migration; do
not claim compatibility merely because both formats can represent the same
fields. Reload and snapshots remain unsupported until their actual VM
semantics are implemented, regardless of the new envelope's capabilities.

**Follow-up and acceptance:** pin the recovered framing/message definitions,
document the NIL profile and extension allocation, and test independent
C++/Python/JavaScript golden frames before wiring the live endpoints. Require
bounded payload/depth/container counts, strict CBOR and UTF-8 validation,
partial/coalesced frames, truncated/disconnected peers, unknown versions and
capabilities, bounded backpressure, and clean owned-handle teardown. Verify
the same handshake/status/error transcript on Linux and Windows and keep
existing-client compatibility explicit. Only then change the default control
wire format. These are migration gates, not features claimed complete.

### Optional-provider parity and ETP codec implementation

This development increment starts from `59f9c2c93073d96aed8d58bc75807da4de02ef2a`.
It supersedes the earlier GTK/PostgreSQL implementation gaps, not the earlier
SDK's provenance or the Linux-only release policy. The previously accepted
`71a9338ec3a7` SDK/VSIX remains unchanged; these new build-tree results are not
installed-SDK, editor or debugger requalification for the new bytes.

**PostgreSQL:** Windows now compiles the shared provider with libpq headers,
uses bounded Winsock readiness waits and numeric address validation, and links
`ws2_32.lib` through runtime, AOT and standalone linker routes. The build cache
fingerprints that import library, including content changes with preserved
timestamps. The stalled-loopback client fixture checks eight bounded timeout
and peer-EOF cleanup cycles. No existing database or service is used.

The isolated fixture uses a private Unix socket on POSIX and numeric loopback
with SCRAM and a fresh secret on Windows. Direct administrative `postgres`
startup was correctly rejected. The repaired Windows fixture uses PostgreSQL's
own `pg_ctl` restricted-token startup inside a no-breakaway owned Job, verifies
the exact private data directory and retained postmaster identity, and requires
successful launcher completion and captured server readiness. It rejects a
wrong password, accepts the private credential, requires all six LLVM-enabled
routes, then verifies graceful shutdown and empty-Job cleanup. Fixture-local
`lc_messages='C'` stabilizes the readiness marker without changing host locale.
The final 16 fixture unit tests include a launcher-exit readiness race.

Windows PostgreSQL 16.15 passed all six routes with zero skips in the first
strict real-server run (15.53 seconds); the final fixture revision is included
in the broader Windows regression run below. Linux's final focused run also
passed all six routes. Failed private fixture directories remain diagnostic
artifacts with the initialization password removed; successful clusters are
removed after owned shutdown.

**GTK:** Windows now shares the real GTK4 provider, widget/event behavior and
lifecycle checks with Linux. Selection uses an absolute wide-character
`NIL_UI_GTK_DIRECTORY`, or executable-adjacent DLLs, and scoped loader flags;
explicit invalid selection fails closed. Direct GTK/GObject/GLib modules must
match their selected files. This is not a complete isolation claim for already
loaded transitive dependencies in arbitrary embedding applications.

Ten real Windows loader controls pass, including Unicode paths, invalid and
ambiguous deployments, and exclusion of ambient current-directory/PATH lookup.
The Windows display harness has 42 portable unit checks. Explicit or adjacent
GTK deployments are checked before an optional headless skip, and all errors
after a display fixture starts remain hard failures. `--require-display` forbids
skipping in qualification runs.

The SSH service session exposed an upstream GTK crash at `gtk_window_new`,
reproduced outside NIL using a minimal ctypes caller after successful
`gtk_init_check`. The provider now rejects a nonvisible window station before
GTK initialization. Its real Windows negative probe passed **39 checks**,
including repeated failures with GTK still uninitialized. No private desktop
or display simulation was substituted for interactive validation.

With explicit user approval, a unique, limited-privilege InteractiveToken task
ran in the existing `nil` console session 3 / `Default` desktop. It used no
password, global input, foreground manipulation, desktop switch, policy change,
or persistent server. The outer process Job and every fixture Job were bounded;
keyboard/close messages targeted only verified Job-owned HWNDs with retained
process identity. Results:

- C++ provider: **4,470 checks passed**; lifecycle: **235 checks passed**.
- Unchanged language desktop cases: **6 VM/native passes**.
- Unchanged keyboard/close-request cases: **4 VM/native passes**.
- All five display stages and the loader prerequisite exited zero with
  `cleanupVerified=true`; outer Job cleanup also passed.
- Task `NIL-Parity-Desktop-7bb0dce948ba4f3aac63aef3ac7e5351` was removed after
  completion, with `LastTaskResult=0`. Removal alone is not cleanup proof;
  the independent owned-process evidence is required.

Guest evidence is under
`C:\provision\parity-ui-v4-7bb0dce948ba4f3aac63aef3ac7e5351`.
The copied `parity-ui-v4-evidence.tar.gz` SHA-256 is
`6810e021dbc9a2a5262e7a3bbccc006f5d9fe56fc560d34fae9b7706d9d0d88a`.

**Dependency provenance:** optional runtimes were provisioned only under
`C:\provision\tools`; no installers, global PATH changes, system services or
new accounts were used. They are not automatically included in NIL SDKs.

- GTK 4.24.1-1: full MSYS2 UCRT64 dependency closure (82 packages), each archive
  checked against the exact size/SHA-256 in a frozen official HTTPS repository
  database. Database SHA-256:
  `15f2152900bf4be320509902e24bb1767e7cf6916b3c871c8391bec766b72cd6`.
  Transferred prefix archive SHA-256:
  `586a6d50823061e720d931c251013916950152fadfafe14e70250d7599e39b23`.
  Destination: `C:\provision\tools\gtk-ucrt64-4.24.1-parity\ucrt64`.
  Extraction validated paths and materialized only in-prefix file symlinks;
  licenses/data were retained. Only GLib schema compilation was run, not package
  installation hooks. This is hash verification, not a PGP-signature claim.
- PostgreSQL: official EDB
  `postgresql-16.15-5-windows-x64-binaries.zip`, 373,254,386 bytes, SHA-256
  `43bb45f173a6f08cf1d29a97a6d8deb119e8e8093a24c00d2d1001a0ccaa8281`.
  Destination: `C:\provision\tools\postgresql-16.15-5-parity\pgsql`.
  Archive paths/CRC/case collisions were checked before extraction. These PE
  files have no Authenticode certificate; no independently published checksum
  or signing assurance is claimed. The existing VC14 runtime remains a VM
  dependency.

**ETP stage 1:** `common/etp_codec.hpp` implements the observed five-byte ETP
envelope plus a bounded CBOR subset, with no I/O or application opcodes.
Ceilings are 64 KiB payload, depth 32, 1,024 container entries and 4,096 nodes.
It preserves full CBOR integer ranges, finite floats including negative zero,
binary data and strict UTF-8 text including NUL. It rejects duplicate map keys,
unsupported tags/simple/indefinite values, invalid UTF-8, nonfinite floats,
trailing data and truncated frames. Partial/coalesced streaming and callback
failure/reentry boundaries are tested. Deterministic output is deliberately not
claimed to use RFC shortest-float serialization.

C++ and independent Python tests pass on Linux and Windows: 13 exact golden
frames, 17 literal anchors, 20 accepted alternate encodings and 26 rejection
vectors. Linux ASan/UBSan and independent cbor2 checks also passed during review.
This is codec interoperability evidence, not completion of Liz's application
protocol or a live NIL ETP endpoint. The repaired experimental JSON status
client still speaks the interpreter's existing handshake/request/result schema;
portable Unix-socket/named-pipe fixtures cover bounds, partial I/O and teardown.
Reload and snapshots remain unsupported and send no misleading control request.

The proposed next profile is explicitly versioned `nil.control/1`, with separate
endpoint/protocol selection, negotiated capabilities and limits, unsigned
correlation IDs, and mutually exclusive results/structured errors. Start with
qualified status operations only. Document NIL-specific message-type allocation
without repurposing Liz's inference opcodes. An advertised PID is not OS peer
authentication; verify peer identity before exposing state-changing operations.
The previous migration acceptance gates still apply.

**Regression checkpoint:** Linux registered 557 tests: **556 passed**, zero
failed and only the optional Xvfb display test skipped (398.82 seconds). After
the last Windows guard and fixture changes, the full Linux build and all ten
affected focused CTests passed again (9.99 seconds, no skips). The final Windows
overlay is `parity-overlay-v4.tar.gz`, applied only after every prior task-owned
guest file matched its recorded overlay. Its full regression run is in progress
at this implementation checkpoint; no full-suite result is claimed yet.

**Remaining parity/release work:** re-seal and rerun installed SDK, Unicode
consumer, debugger and editor gates against the new revision; qualify a bare
Windows host and the reviewed toolchain/dependency closure. The approved MSVC
telemetry workaround remains part of this VM's provisioning, not proof of an
unmodified MSVC installation. Inspect ordinary file/path/environment Unicode
and separator behavior beyond the strict byte-file APIs. Windows atomic byte
publication still has weaker parent-directory pinning/default-DACL guarantees
than POSIX; experimental nilagent window search ignores its title filter.
Optional HAG Windows loading remains unqualified. Native debugger attachment,
general FFI, broad networking/streams and screenshot placeholders are shared
limitations, not Windows-specific regressions. No Lich publication, SDK
activation, Anomalous changes, model/GPU workload or long-lived server occurred.

The next ordinary-I/O investigation should use one public-API Ny consumer with
three modes: lexical paths; whole-file plus File stream round-trips; and
environment plus cwd. Run text and binary interpretation on each OS (12 bounded
executions), with independent host checks of exact filenames and bytes. Include
ASCII controls, Unicode filenames/parents, slash/backslash/mixed separators and
single-element `join`; keep drive-relative/UNC semantics separate from portable
expectations. Environment's existing wide Windows implementation and Unicode
fixture provide a positive control. Narrow ordinary opens, host filesystem path
parsing and `current_path().string()` are investigation targets, not reproduced
bugs. General native support for these APIs is a shared limitation, so do not
turn interpreter-only evidence into a native-parity claim.

### Suspend-overlap regression failure retained

Implementation `af38ebd2920d3c322ca79f6db3827354b8d8af52` was committed and pushed.
The first final Windows build-tree run registered 560 tests: 558 passed, one
service-session UI skip, and one failure in `ny.routes.numeric-value-ordering`.
It is **not** a clean qualification run. The JIT route reported a timeout after
1,462.74 seconds; the other five routes passed. Host journal evidence records
suspend at **2026-11-02 23:06:55 UTC** and resume at **23:31:16 UTC** (1,461 seconds).
The route directory spans 23:06:46–23:31:17. This strongly supports a
suspend-overlap timeout, not 24 minutes of measured compiler CPU time; it does
not by itself exclude another defect.

Three subsequent strict repetitions kept the original 10-second per-command
budget and all six routes: **18 passes, zero failures/skips**. JIT durations were
0.512007, 0.501341 and 0.500252 seconds. The repetitions ran inside an outer
bounded owned Job. The failed report and artifacts were not relabeled or
discarded. The full run lasted 3,531.78 seconds including suspend; its archive
SHA-256 is `4ee575a584e974c3bb34a141116a6c10c935dcf796f97a3a4f7c0e0b9452bcc3`.
Evidence is retained in
`/home/nathan/vm/nil-win11/artifacts/af38ebd2920d-parity`, including the independent
host suspend log, original route report, all three repetitions, full JUnit/log,
final PostgreSQL fixture, headless guard and interactive UI evidence. No host
power policy was changed.

Review also found independent legacy harness debt: `ny_route_matrix.py` kills
only the direct Windows child and then uses an unbounded final `communicate()`;
the generic route CTest registration lacks an outer timeout. This is confirmed
cleanup/deadline debt, not a demonstrated cause of this particular stall.
Harden it with owned process-tree cleanup, bounded capture/drain and explicit
outer limits in a separate tested slice; do not silently expand the test budget
or hide a failure as an infrastructure skip.

Before SDK construction, the guest's 23 overlaid files were compared byte-for-byte
against the known v4 overlay and pushed commit. All other changed paths had to
match the old HEAD. A unique backup preserved all 32 targeted files, the new
index was verified identical to the pushed tree, and `--ff-only` advanced the
guest to clean `af38ebd` without reset/discard. The helper explicitly refuses
disabled Python assertions and was invoked with isolated Python. The fresh SDK
pipeline requires a new full CTest report; the suspend-overlap report cannot
qualify its package.

### Fresh development SDK acceptance (in progress)

The clean `af38ebd` SDK pipeline completed its new full CTest run in 505.24
seconds: **560 registered, 559 passed, zero failed, one expected service-session
`nil.ui-display` skip**. PostgreSQL is now a passing test rather than an allowed
skip. The archive verifier's exact registration contract was updated to 560
tests and only that UI skip; 25 synthetic verifier checks pass. These verifier
tests are not themselves package qualification.

The fresh candidate is
`C:\src\exec-nil-framework\build\windows-candidates\development-c99de433bcf94b52888fe7bbea2153b8`.
Its manifest SHA-256 is
`3041e65b9e4286b307b9e8c009f141e4be53f6a09525a35fbd95cdc0f641d4bb`:
11 root executables, 18 compiled library modules, 66 HTML documentation pages,
259 sealed payload files (85,042,444 bytes). Installed debugger **146 checks**
and Unicode **16 routes** passed with before/after SDK seals. The independent
archive/consumer reconciliation and 1,184 installed route matrix are still
pending at this note's checkpoint.

The installed editor run failed at `external import change refreshed Problems`.
The unchanged 88-check suite, source and extension bundle were retained; no
timeout, exclusion or retry-as-pass was substituted. The original report and
profile logs are preserved in `parity-editor-failed-af38ebd.zip`, SHA-256
`b899fad94085917ba4640496382aa18331094b4a2fc025cac662b45762a0055a`.
Inspection found two branches needing runtime evidence: preview tabs may leave
no open entry eligible for analysis; a cached clean closed-tab import can also
veto disk-snapshot publication. Empty Problems after a save is not proof a fresh
analysis finished. An additive diagnostic wrapper will observe tab/document,
watcher and compiler state without modifying the committed suite or budgets.

The first installed desktop task stopped at preflight: the staging SDK's
`nillib` directory is private to the build account and not readable by desktop
user `nil`. No GUI fixture started; preflight Job cleanup was verified and task
`NIL-Parity-Desktop-4acb2b752ffe448bb011efda1e55a8e9` was removed. Do not relax
staging ACLs. The replacement gate extracts the sealed ZIP as `nil` into a
fresh private evidence directory, hashes and parses the same bounded archive
snapshot, verifies exact manifest/payload membership and hashes, and uses
exclusive writes. Extraction and all acceptance phases share the 690-second
budget beneath the 790-second controller and 840-second scheduler deadlines.
Twelve private tests cover these helper boundaries; actual extracted-SDK desktop
acceptance remains pending.

### VM servicing interruption during installed acceptance

At approximately **2026-11-03 00:15 UTC**, new SSH connections stopped receiving
the guest banner. The QEMU management endpoint remained responsive and reported
`running`; the console framebuffer was black. Host kernel logs showed no new
suspend/OOM event in that interval, and the host had available memory with no
ongoing swap activity in the sampled interval. These observations do not
establish a guest-kernel, SSH, resource-pressure, or editor root cause. Once
SSH returned, the System log established the interruption was Windows servicing:
`TrustedInstaller.exe` initiated planned `Operating System: Upgrade` restarts,
with fresh boots at 00:14:56, 00:16:15 and 00:17:12 UTC. The restarts began
before the approved ACPI shutdown attempt; do not attribute them to that request
or to an observed compiler/editor crash.

The last pre-interruption consumer progress sample contained 73 per-fixture/library
reports. There is **no completed 1,184-execution result or delivered ZIP**.
The diagnostic editor rerun used the unchanged gate driver and full suite,
with its additive observation wrapper under
`C:\provision\parity-editor-diagnostic-af38ebd` and evidence directory
`C:\provision\vscode-installed-sdk-RuhILg`. Its final result is unavailable.
Do not convert either interrupted/unknown run to PASS. The extracted-SDK
desktop task has not been started.

The user explicitly approved a graceful VM restart, but not a forced reset.
At **00:17:26 UTC**, the host delivered QEMU's `system_powerdown` ACPI request
and received its `POWERDOWN` event. That event is delivery of the request, not
evidence Windows shut down. The first successful new SSH probe returned at
00:18:44 UTC. No force-reset, guest-process kill, disk mount, power-policy
change, or duplicate VM was attempted. After confirming the new boot and no
remaining test/compiler/editor processes, the two stale task-owned **host SSH
clients** were closed. Source and all candidate/evidence files remain in place.
The recovered route directory has 74 reports, not a completed consumer result.

At 00:21:26 UTC the guest reports Windows 11 Pro 26H2, build **26300.9711**,
with neither the Windows Update nor Component Based Servicing reboot-pending
marker set. The repository remains clean `af38ebd`. No user is signed in after
the upgrades; the user was asked to sign `nil` back in for the approved desktop
tests. No password, autologon setting or update policy was changed. System
events, state, original CTest/build evidence, incomplete consumer reports and
retained file inventory are copied in
`parity-servicing-interruption-af38ebd.zip`, SHA-256
`b89f68f7a1345f84d4501d7fa0ddbd9794a384c11a0641f5b4dd524bae8f5bd6`.

Independent host reconciliation of the copied debugger evidence passed all
146 exact named checks, 20 command identities and 13 frozen source hashes;
27 mutation/positive controls also pass. This remains debugger-only evidence,
with final ZIP binding pending and no editor acceptance implied. The additive
editor diagnostic wrapper passed four mock controls for unchanged invocation,
results and cleanup; these are not actual editor acceptance results.

Private helper sources/tests are preserved in `parity-gate-tools.tar.gz`,
SHA-256 `6fe9412b0a86e7d49b29539094697a1e3e97843b13340411277279e22ee05f0e`,
under `/home/nathan/vm/nil-win11/artifacts/af38ebd2920d-parity`.
The prior accepted `71a9338ec3a7` SDK ZIP and VSIX were rehashed unchanged:
`872cf43be600e25b09a35c9cc161e341dd8b9887a48afdf253e886705dcd1447` and
`b146c9c3b708d4e3fa1a093f8500e87b19037e303cfd482987b4c51122379f57`.

After guest access returns, first capture the unfinished consumer/editor logs
and reports, verify source remains clean `af38ebd`, and inspect owned process
cleanup. Preserve the first failures. Rerun interrupted acceptance with fresh
evidence paths and the same original budgets; diagnose the editor's import-tab
state before any fix or qualification claim. Only after all required consumer
checks complete should the final ZIP be created, independently verified and
used for the already approved limited desktop extraction/test task.

Recovery resumed the interrupted last SDK step against the **same sealed SDK**,
after validation of its unchanged manifest/payloads. The entire consumer matrix
is rerunning from scratch, not completing only the remaining half. New logs are
`C:\provision\parity-consumer-post-upgrade-af38ebd*.log`; the original incomplete
reports remain untouched. An additional full post-upgrade CTest run writes
`C:\provision\parity-ctest-post-upgrade-af38ebd.xml` and `.log`, without replacing
the original passing CTest report bound into the SDK manifest. The diagnostic
editor rerun uses fresh evidence `C:\provision\vscode-installed-sdk-uKCveZ`.
No test budget or accepted check set was changed, and these resumed runs are
not yet claimed complete.

### Editor import-cache regression and separate editor revision

The post-upgrade diagnostic extension-host run completed all **88 named checks**
with zero exclusions against the unchanged `af38ebd` SDK and editor source.
Before/after SDK seals and extension/configuration identities matched. Its
passing trace shows `first.ny` remained an eligible preview tab; the clean,
tabless dependency cache lagged the external disk edit by about 50 ms, and
Problems refreshed after about 8.67 seconds behind queued project analyses.
The observation budget dropped 53 later events. This does **not** establish the
cause of the original untraced failure; that failure remains preserved.

A separate deterministic unit regression then exposed a real publisher bug:
`workspaceInput` excludes clean closed-tab caches, but `publish` could reject a
matching current disk/compiler snapshot because that nonauthoritative cache
still contained old text. The test pins an eligible entry and uses the actual
production publisher, overlay builder and byte mapper with a real private
Unicode/CRLF file. It failed before the fix (zero located findings instead of
one). The minimal guard now consults cached text only for an open tab; dirty
buffers and mismatched disk contents still suppress unsafe locations. It does
not edit/save the cached document or broaden source-map trust.

All **eight focused regressions** and **162 editor unit tests** pass on Linux,
including absent/current caches, dirty/open-tab mismatches, changed disk, and
buffers becoming dirty/open during analysis. No host-suite assertion, deadline,
88-check inventory or compiler/SDK payload was changed. This editor-only fix
will be qualified as a **separate clean editor source revision** against the
frozen `af38ebd` language SDK, with both identities explicit in its evidence;
the SDK must not be relabeled as a newer revision. Nineteen private gate tests
verify this split provenance while retaining the original acceptance checks.
Windows validation of the newer editor remains pending at this checkpoint.

### Post-servicing rerun: installed routes pass, project guard finds DLL variation

The clean `af38ebd` SDK completed the restarted installed consumer matrix:
**74 fixtures × two library layouts × eight execution routes = 1,184 PASS**.
The final before/after validation retained manifest SHA-256
`3041e65b9e4286b307b9e8c009f141e4be53f6a09525a35fbd95cdc0f641d4bb`,
all 259 payload files, 11 root tools, 18 compiled library modules and 66 HTML
documentation pages. The original interrupted reports remain preserved; the
completed `consumer-routes.json` is copied into the host `af38ebd2920d-parity`
evidence directory. This is installed consumer evidence, not release acceptance.

The separate post-servicing full CTest run instead reports **560 registered,
558 PASS, one service-session UI skip, one FAIL**. The failing test is
`nilbuild.project-contract`: its first compiler process listed
`C:\\WINDOWS\\SYSTEM32\\apphelp.dll`, while its post-build analysis did not.
`nilbuild` correctly refused publication with `compiler module graph changed`.
The report is preserved as `parity-ctest-post-upgrade-af38ebd.xml`, SHA-256
`f163e0fdbaaff768d0b523ff6067b29d6518f75f5a1688093bf51f96478f3a10`.
The SDK's original passing pre-servicing CTest report is not overwritten and
does not supersede this later failure.

The Windows collector snapshots every loaded DLL from each independent
compiler process. This failure establishes different observed module sets,
not deletion or mutation of the DLL on disk and not causation by Windows
servicing. A generic module-set inconsistency had also appeared earlier, but
that older evidence does not identify this DLL. The bounded correction under
review always fingerprints the actual system-directory `apphelp.dll` when
present, retains all other observed modules, and collapses aliases only after
verified same-file identity. No `nilbuild` module-set or byte-digest guard is
to be weakened, and any compiler change requires a new SDK candidate rather
than relabeling or modifying this frozen one.

### Updated editor passes Windows integration against the frozen SDK

Clean editor revision `0628f6a4c6ecb2e121647c2158a478645469d71e` was
fast-forwarded into the VM only after the original full CTest finished. Its
three changed files (notebook, analyzer, publisher unit tests) do not change
the in-flight installed-route harness, source fixtures or SDK payloads.
Windows editor units report **161 PASS, one POSIX-FIFO-only skip, zero FAIL**;
all eight new publisher regressions pass. The production extension build then
completed the unchanged **88-check Windows extension-host suite with zero
exclusions**, seven SDK tools, 31 actual tasks covering all eight task kinds,
and two debugger sessions. Source and SDK identities stayed unchanged before
and after; no Code processes remained after the bounded runner completed.

The split-revision gate used the original lightweight observation wrapper,
not the heavier diagnostic instrumentation. Complete host evidence is in
`/home/nathan/vm/nil-win11/artifacts/af38ebd2920d-parity/editor-source-0628f6a-complete/`.
An independent host reconciliation verified the frozen 78-file editor Git
inventory, exact harnesses, SDK seals/configuration, all named checks and all
17 VSIX assets against the Windows-tested production bundle. Its 46 synthetic
negative/positive boundary tests also pass; these are verifier tests, not
additional Windows integration checks.

- Editor gate SHA-256: `29c1a44429bd8958d4c29b753c560cbfdd03300dfa9d6178df57900b75f5a4d2`.
- Tested extension bundle SHA-256: `844aaee30df62134efa971c9bf1d3e9a1e71ca39f3665f1a3ee3d0f9e13fd500`.
- `nil-framework-editor-0628f6a.vsix` SHA-256: `4e6ad286ec6707131f869ea6ebd1fa4df18ae1bf40be551d6a03f4b5ad2ef3e1`.
- Independent input anchor SHA-256: `3811aae20dfa40cd638b7e54ad87e675faf798ad317f42c17bb4e822e5c5b6e9`.

This proves editor integration with the frozen `af38ebd` SDK, not qualification
of a future compiler build or full SDK delivery. The new candidate remains
unaccepted due to the separate compiler-module inconsistency; the previously
accepted `71a9338` SDK ZIP and VSIX remain byte-identical. Installed GTK desktop
validation still requires the user to sign `nil` back in after Windows
servicing; no credentials, global input or autologon settings were used.

### Bounded compiler-module fingerprint correction

The Windows collector now adds `GetSystemDirectoryW()/apphelp.dll` when it is
a regular file, without loading it or consulting environment/search paths.
Only verified same-file aliases collapse to that anchor; different app-local
files with the same name or contents remain tracked. Genuine optional leaf
absence is distinct from invalid parents, permission errors or I/O errors.
Identity failures leave the original set intact and make discovery incomplete.
The Toolhelp terminal error is captured before any subsequent filesystem/API
call. This stabilizes the identified optional component, not every possible
loaded-module variation.

`nilbuild` itself is unchanged: exact module-set comparison and content digests
still guard publication. A new regression mutates a private fixture named
`apphelp.dll` during compilation while retaining its size and timestamp, and
requires rejection plus byte-identical previous artifact and state. The
existing arbitrary-module-change preservation test remains in place. No system
DLL is modified by any test.

Six focused Linux CTests pass on the stable rebuilt compiler, including the
project, incremental, dependency, toolchain and project-edition contracts. The
new portable unit supplies 23 checks; independent ASan/UBSan execution and
strict MinGW syntax validation also pass, and peer review found no remaining
issues. Windows runtime/API validation, stability repeats and a newly sealed
SDK candidate remain pending at this source checkpoint.
