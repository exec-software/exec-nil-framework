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
