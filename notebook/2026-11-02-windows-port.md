# Windows framework recovery and development SDK

## Status

The latest Windows development SDK is built from clean, committed and pushed
source `5630388`. Its complete SDK pipeline and independent archive verification
passed: 571 registered CTest cases, 570 passed, zero failures and one service
desktop skip; installed validation and 1,184 aggregate consumer executions also
passed. It contains 259 sealed payload files, eleven root tools, eighteen binary
standard-library modules and 66 compiled documentation pages. Separate installed
GTK acceptance passed ten VM/native cases and 235 lifecycle checks. Installed
VS Code acceptance passed all 88 checks with no exclusions, 31 tool tasks and
two debugger sessions, including four ordinary Unicode profile output paths
exceeding 260 UTF-16 units. Independent host replay binds both desktop gates to
the accepted SDK archive; scheduled-task removal, owned process cleanup and
unchanged inputs are verified.

The SDK includes the `164f179` Aperture long-report-path fix, `f4d5d04` source-scan
repair, `262ceda` import-search optimization and `5630388` shared immutable test
record reader. The import fix reduced observed raw Windows analysis from
8–9 seconds to 1.4–1.5 seconds while preserving analysis output apart from compiler
provenance. The Linux full suite at `262ceda` passed 565 tests with three
prerequisite skips, and both contracts changed by `5630388` passed afterward.
Earlier interrupted/failed pipelines and editor diagnostics remain preserved;
their evidence has not been relabelled as successful acceptance.

The Linux reference has now been refreshed at the same `5630388` source:
568/568 CTests passed without skips, including PostgreSQL and isolated GTK;
SDK validation and 1,184 installed consumer executions passed. Both platforms
passed fresh installed ordinary-I/O, Unicode, debugger and same-process
checkpoint checks. The inventory comparison matches 566 common CTests,
74 consumer fixtures, 80 standard-library source files and eighteen compiled
module identities. Fresh installed Linux GTK also passed ten VM/native cases.
The historical `d5e8758` evidence remains attached to its original SDKs.

This is development evidence, not a v1/RC or unrestricted parity declaration.
The earlier zero-exit, empty lifecycle-output observation remains preserved and
unexplained. No active SDK/VSIX, Lich publication or Anomalous checkout has been
changed by these recovery steps. Checkpoints retain the documented deterministic
same-process restrictions; external-state rollback, code hot reload and native
`environment.list` remain unsupported.

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

### Fingerprint runtime checks and reproducible LLVM DIA configuration

The `d9f8c31` Windows fingerprint unit builds and passes **46 checks**, including
the actual system-directory API, loaded-module snapshot and file identities.
Its JUnit SHA-256 is
`5ba2c04e57def33f31618339d7ac9024a87762697cb5af9c2408b3e4c9b1d818`.
Full Linux regression reports **558 registered, 556 PASS, two SKIP, zero FAIL**:
optional Xvfb and PostgreSQL tools absent from that invocation's environment.
The latter was then separately configured using existing
`/usr/lib/postgresql/16/bin` and `/usr/share/postgresql/16`; all six PostgreSQL
routes passed with graceful shutdown and owned-process cleanup. The original
full report is not relabeled: SHA-256
`200da534adaf0f88b5cd5cd77b390dc3374a538cd53937d6c5a9962012a2b64a`;
supplemental PostgreSQL JUnit SHA-256
`c591f250228b17d63e3bbf7fc72662eaa8f8e2a771020ddfd0e5bd4a033e53c8`.

The full Windows rebuild stopped before its focused tests because CMake
regeneration restored LLVM 18.1.8's exported `LLVMDebugInfoPDB` dependency on
the missing `Visual Studio/2019/Professional/DIA SDK/lib/amd64/diaguids.lib`.
The installed 2022 Build Tools copy exists. The preserved failure is
`artifacts/d9f8c311c948-parity/parity-apphelp-build-d9f8c31-failed.log`; no claim
is made about why earlier generated files did not expose this stale export.

`NIL_LLVM_DIA_LIBRARY:FILEPATH` now provides a scoped, explicit configure-time
repair for that one missing literal imported link entry. It persists in the
CMake cache, preserves every unrelated link entry and ordering, and leaves
an existing valid DIA import unchanged. Missing/relative/invalid replacement
paths, ambiguous duplicates and unsupported DIA expressions fail configuration.
No vendor LLVM file, global search path or machine setting is edited. The
Windows build instructions document the selected x64 DIA library. All 23
isolated configure tests pass on Linux; Windows configuration and subsequent
compiler/SDK qualification remain pending.

The user signed into Nathan after Windows servicing. Session 1 is active and
the account resolves to SID `S-1-5-21-1260315539-3167597677-3402283352-1002`.
The same approved bounded desktop tests will use that limited interactive
token instead of the earlier `nil` account. SDK staging permissions remain
unchanged; a fresh sealed extraction will be used for desktop consumption.

### Clean 45e7b9a core SDK and installed-tool validation

Windows configuration now succeeds with the explicit 2022 DIA library, and a
second configuration without repeating the flag retains the cached selection.
The full 93-step MSVC rebuild passes. Seven focused CTests pass, followed by
three separate fresh repetitions of both project-contract and project-edition;
all pass without retrying failed tests. The Windows fingerprint unit executes
46 checks and the DIA configure contract executes 23. The earlier failed build
and supplemental `af38ebd` CTest remain preserved.

The complete, source-frozen Linux run at `45e7b9a` reports **559 registered,
558 PASS, one optional UI/Xvfb SKIP, zero FAIL** (420.17 seconds). PostgreSQL's
six routes and cleanup run inside that full invocation. Its JUnit SHA-256 is
`4af27d7050e66074cb2c26569b46b9904888e5d4e3e4d8e05ca1abac8d842d72`.

The new Windows candidate is
`C:\src\exec-nil-framework\build\windows-candidates\development-93a282394dcb4d1b8fd8f4aca0522d5a`.
Its complete SDK pipeline exits zero: **562 registered CTests, 561 PASS, one
service-session `nil.ui-display` SKIP, zero FAIL**, followed by **1,184 installed
consumer executions PASS** (74 fixtures, two library layouts, eight routes).
The temporary consumer directory is removed by the successful harness. The
candidate contains 11 root executables, 18 strictly checked compiled library
modules, 66 documentation pages, and 259 sealed payload files plus the manifest.
No active SDK or Lich/Anomalous deployment is changed.

The independently checked host archive is
`/home/nathan/vm/nil-win11/artifacts/45e7b9ad0216-parity/nil-framework-windows-development-45e7b9ad0216.zip`
(28,532,141 bytes, 260 entries), SHA-256
`b7a096df65caa9000c74ce1bf5249e8fc0ec221dcb9f082fd89e7948d69817f7`.
Manifest SHA-256 is
`19f15d4dd447ad5db76fec32d7e7d7185bf4c8957aa9ae8987364398e5236876`;
CTest JUnit SHA-256 is
`81769f849bf81adb6119f9ff44e87fdbfc2a44f8ea83dfd1aa49cba34aab9c0f`;
consumer report SHA-256 is
`d94b26442ae3ac6697f7597063e949810ccf5b7eac7d74b48073cb03a431964d`.
Host verification checks every archived payload, exact root layout, build/tool
identities, all registered CTest names, the sole documented skip, and the complete
consumer result. Dependency inventory remains evidence, not bare-host certification.

Additional installed-tool gates against this exact manifest pass:

- Debugger: **146 named checks, 20 commands**, with 13 frozen source identities.
- Strict Unicode byte-file paths: **16 routes, 34 commands**, both library
  layouts, unchanged before/after origins and owned temporary-directory cleanup.
- VS Code: **88 real extension-host checks, zero exclusions**, 31 actual tasks
  covering eight task kinds, seven SDK tools and two debugger sessions. Windows
  unit tests report **161 PASS, one POSIX-FIFO-only SKIP, zero FAIL**.

The editor bundle remains SHA-256
`844aaee30df62134efa971c9bf1d3e9a1e71ca39f3665f1a3ee3d0f9e13fd500`.
The `0628f6a` and `45e7b9a` editor Git trees are identical; the handoff VSIX is
an explicitly recorded byte-identical copy, not a newly rebuilt package:
`editor-complete/nil-framework-editor-45e7b9a.vsix`, SHA-256
`4e6ad286ec6707131f869ea6ebd1fa4df18ae1bf40be551d6a03f4b5ad2ef3e1`.
The final Windows editor gate hash is
`680fc03b571f1c1c9657097a7a7671e428295c5b18e66644ca75adaa73525bd3`.
Complete copied settings, host report, exact lightweight harnesses, frozen input
anchor and the independently reconciled 17 VSIX assets remain in `editor-complete`.

`independent-core-delivery-45e7b9ad0216.json` joins the actual archive, debugger
and Unicode evidence without rewriting earlier pending-only reports. Its hash is
`70d37478cc49b0310d5926a40d5c89fd86aed1535df0fed517a0954c83a20ec2`.
This is core development delivery evidence, not complete desktop or release
qualification. The old `af38ebd` candidate now has an additive `FAILED.txt`
delivery-rejection marker outside its sealed SDK because of its later project
guard failure. Its SDK, passing consumer/editor reports and failure evidence
are all retained; it cannot be archived by the delivery helper.

### Nathan desktop run: language cases pass, lifecycle completion remains open

The approved one-off task used Nathan's verified SID ending `1002`, an
Interactive/Limited principal, a private evidence directory and bounded owned
Jobs. It verified the final archive hash, extracted all 259 payload files into
a fresh desktop-owned SDK, and validated the SDK before and after. No password,
autologon setting, staging ACL, global input or unrelated window was used.
The scheduled task was removed and owned-process cleanup was verified.

All **ten installed GTK VM/native language cases passed**, including targeted
keyboard and close events through verified Job-owned HWNDs. The supplementary
build-tree provider helper reported **4,471 checks**. That number is not a fixed
test inventory: `wait_event()` increments the assertion counter on each poll,
so the private verifier's expectation of exactly 4,470 was incorrect. This does
not justify reducing the source assertion set or ignoring completion output.

The separate build-tree lifecycle helper exited zero but produced **zero bytes
on both stdout and stderr**, instead of its final `UI lifecycle: 235 checks
passed` summary. The outer display harness accepted exit zero; the independent
desktop reconciliation did not. Full desktop acceptance therefore remains
**pending**, despite successful installed language cases and cleanup. Neither
the core CTest report's skip nor this first incomplete lifecycle result is
relabelled as full desktop coverage.

Original evidence remains in `desktop-complete`, including the entire fresh SDK.
The nested UI report hash is
`1e8953f51305dc0900acfe309eda1ec4b49a2676a6ce9ab0e768e3b2c90ccc14`.
The first reconciliation failure is preserved separately as
`desktop-reconciliation-first-failure.json`, SHA-256
`fb32325e587c612124f8fc8cc4f73983be2b140ba61cafeb58e7a7c829c937df`.
Independent before/after provider binary anchors are identical. The 45 synthetic
desktop-verifier controls test evidence rejection, not actual GTK acceptance.

A separate, bounded diagnostic task then ran the **unchanged original helper
once** and a private instrumented copy once. The original emitted the expected
32-byte lifecycle summary. The instrumented copy retained every original
assertion and recorded flushed, exclusive-file checkpoints through text/binary
VM lifecycles, native lifecycles, all **235 checks**, and return zero. C++ stream
state stayed good; the explicit final flush and direct Win32 stdout write also
succeeded. Both Jobs cleaned up and the diagnostic task was removed. These
observations do **not** establish why the first original run emitted nothing;
the extra flush/instrumentation is not a production fix or replacement proof.

The complete private build/source/logs are retained in
`lifecycle-diagnostic-complete`. Diagnostic progress SHA-256 is
`4361752f91a8fdde0bf7705b76d4259527b7d82ad4cc2c49b45d2ddafc6df8e6`.
The committed display harness now requires each provider/lifecycle helper's
complete summary after successful exit and owned cleanup, before starting the
language cases. Provider counts are canonical positive ASCII decimals bounded
by the C++ signed-int counter and recorded as observations, not a fixed coverage
total. The current lifecycle fixture must report exactly 235 checks. Empty,
truncated, duplicate, wrong-mode and incorrect-count output is rejected. No
C++ fixture, runtime assertion, input method, timeout or cleanup rule changed.
Two main-path regressions demonstrably failed before this guard because the old
harness accepted empty output with exit zero. All **50 portable tests pass on
Linux and Windows**, with independent review; Windows ran an exact private copy
of the two edited files plus the unchanged owned-process helper, leaving the
source-frozen guest checkout untouched during evidence collection. Logs and
tested files are retained in `ui-completion-guard` and the red/green logs beside
it. This closes the acceptance hole, not the underlying intermittent output
question. No full desktop run with the new guard is claimed here.

Next: investigate the initial missing summary under the original developer
environment, comparing an explicit nonsecret environment/launch whitelist with
the diagnostic baseline, then rerun the complete strict desktop gate with fresh
evidence. Do not silently retry, waive the first result or weaken completion.

Private gate/diagnostic tools and tests are archived in
`parity-gate-tools-45e7b9a.tar.gz`, SHA-256
`1c1ab50b01c7e1d98d7ca975f8fe10eb718664fc69f2db9acbe8e062dc0eb79f`.
All frozen-SDK evidence collection finished with clean VM source `45e7b9a`;
this subsequent notebook/harness checkpoint does not relabel the archive.
Visual Studio's optional telemetry opt-out remains zero
and the exact optional uploader remains reversibly disabled. Saved PowerShell
policies remain unchanged. No local model/GPU workload, development server,
forced VM reset, active SDK replacement, Lich upload or Anomalous modification
was performed. Windows remains a development target with the previously listed
bare-host, provider, publication and shared-language limitations.

### Follow-up: controlled lifecycle environment and output-handle observations

The next investigation preserved the SDK and original failing evidence. A
private native child recorded its Win32/CRT standard handles, exact binary
input, raw/CRT/C++ output and exit code in exclusive flushed sidecars. The
committed Job ownership/start path with private diagnostic legacy and
explicit-forwarding gate variants preserved all 11 input bytes, 25 stdout bytes
and eight stderr bytes in the normal launch mode. A deliberately detached
legacy launch lost its streams; explicitly
forwarding them preserved them. A deliberately closed child stdout was also
detected. All five children returned the intended nonzero code 19 with verified
Job cleanup. Neither detached mode nor deliberate handle closure is used by
the GTK launcher, so these controls do **not** establish the initial failure's
cause or justify changing production process ownership/stream inheritance.

Complete raw evidence is in `stdio-diagnostic-complete`. Independent raw-log,
sidecar and executable/source reconciliation is
`stdio-diagnostic-reconciliation.json`, SHA-256
`63b6d965e37e720a7925f19c627b10f251d5cf8f6780746080da3ac6432e57fb`.
The actual guest Python version is 3.15.2; its installed subprocess source was
inspected rather than assuming Linux implementation details apply. Six portable
probe-controller tests pass. This is diagnostic evidence, not SDK acceptance.

A separate Interactive/Limited task then compared two fixed profiles: the
scheduled-task baseline and the original `vcvars64` + LLVM PATH-prefix +
process-scoped RemoteSigned environment. Both used the same CMD/Python/owned
child topology, GTK settings and fresh per-profile working-directory structure
(not identical absolute CWDs). Only an explicit nonsecret environment whitelist
was recorded. Each profile ran one unchanged provider, exactly three unchanged
lifecycle observations and one private instrumented lifecycle, with no retries
or changed inputs. The 60-second child, 310-second wrapper and 600-second shared
limits remained below the existing bounded task/controller limits.

All ten planned observations passed: six original lifecycle summaries reported
exactly 235 checks, both instrumented runs reached their final 235-check
checkpoint, and provider counters were 4,471 and 4,472. The different provider
counts reinforce why polling assertions are observations rather than a fixed
coverage total. Before/after executable and scoped source-helper hashes were
identical, all owned cleanup was verified, and the one-off task was removed.
Eighteen portable comparison-controller tests pass. The copied comparison is
`lifecycle-environment-complete/evidence-230a88433bcb4084b47dd400242d277f/environment-comparison.json`,
SHA-256 `1468fb31e1aa91e8621030ee1e1892e52a3ceeafe270432f6ef515a9cdc9d473`.

These bounded observations did not reproduce or explain the first blank
summary. They do not repair or relabel that failed acceptance evidence. The
next full desktop validation must use a fresh sealed SDK extraction, explicitly
distinguish the unchanged `45e7b9a` SDK from the stricter `58352fc` harness, and
require complete helper summaries plus all installed VM/native UI cases.

### Fresh strict desktop run: complete development evidence, prior cause open

One new full run used a fresh extraction of the unchanged SDK archive
`b7a096df65caa9000c74ce1bf5249e8fc0ec221dcb9f082fd89e7948d69817f7`.
The SDK and supplementary build-tree binaries remain from `45e7b9a`; the
execution harness is explicitly `58352fc`, including its required-summary
guard. Five source helper/fixture identities and both C++ executable hashes
were pinned before and after. Both SDK structural validations passed, and the
host independently checked all 259 extracted payload files against the sealed
manifest. No tools, libraries, fixtures or archive bytes were rebuilt or changed.

The run passed **all ten installed GTK VM/native language cases**. The unchanged
provider emitted its complete summary with 4,472 observed checks; the unchanged
lifecycle binary emitted the exact 32-byte, 235-check summary. The two
interaction routes each recorded 27 messages addressed only to verified
Job-owned fixture HWNDs, with the actual Ny keyboard/close assertions passing.
All owned processes were cleaned up and the Interactive/Limited Nathan task
was removed. No global input, password, desktop switch or login change was used.

Complete copied evidence, including the fresh SDK and actual guest helpers, is
under `desktop-strict-complete/evidence-6a6c78bdf6284fca839a3443766be066`.
The nested UI report SHA-256 is
`8e12104f8cee9ed8e64ad430f8ec19c0245060cc31d977c526b8412cad44d9af`.
The independent host reconciliation also rechecked the frozen archive, original
562-test/1,184-consumer delivery authority, exact source/tool identities and the
original failed run's unchanged raw logs. Its additive report is
`desktop-strict-reconciliation.json`, SHA-256
`2c1847bf1f23ff2a9f1481a0c1c75f29baf5eea11497a1a9368f84f4fe48ff6d`.
The verifier is separately retained in `desktop-strict-host-verifier`, with
58 reconciliation and 16 extraction/origin controls passing and independent
review. Canonical variable provider counts are checked against captured output
and integer metadata; empty/duplicate summaries and non-235 lifecycle counts
remain failures. This is a new passing observation, not a rewrite of the first
run or proof that its missing-output cause has been repaired.

The environment comparison's independent reconciliation is also retained as
`lifecycle-environment-reconciliation.json`, SHA-256
`7c0f4e7e548926bca7d7fe63b035932d0b27e1b5c4a67c51551697d6ebd7bb41`,
with 19 positive/negative controls passing. All follow-up private tools, tests
and the inspected guest subprocess source are archived in
`parity-followup-tools-58352fc.tar.gz`, SHA-256
`f188d8c0ab08e796dec954df453549ab69f0762c0d08e7859aefc6fee19260cc`.
These artifacts are additive siblings under the existing
`/home/nathan/vm/nil-win11/artifacts/45e7b9ad0216-parity` evidence root.

The original missing-output cause remains an explicit release risk. The next
feature-parity slice is the ordinary file/path/environment Unicode and separator
consumer described above; bare-host/toolchain qualification and the other
listed provider/publication/shared-language limits are not closed by GTK.
Saved PowerShell policy remains unchanged, and the optional MSVC uploader stays
reversibly disabled with telemetry OptIn zero. No active SDK replacement,
Lich publication, Anomalous change, model/GPU workload or long-lived server was
performed. The code was left unchanged rather than applying a speculative
stdout/runtime fix; this checkpoint adds reviewed evidence to the notebook.

### Ordinary file/path/environment parity: reproduced separator bugs fixed

The next slice used the same frozen public-API Ny consumer and host oracle on
Linux and Windows, with three independent modes (lexical paths, ordinary files
and streams, environment/CWD) in text-NIL and binary-NIL interpretation. It uses
source STL copies, not binary-library or native-route qualification. Each
compiler/child has a 60/30-second deadline, a shared 300-second budget and a
1-MiB output cap. Retained process groups/Windows Jobs own cleanup; an
unverified constructor/cleanup failure prevents subsequent launches. There are
no retries to replace failed observations.

Both baselines collected all six observations with verified cleanup and
unchanged inputs. Linux failed 12 of 25 lexical expectations per encoding;
Windows failed only the single-component `join` expectation. Linux had treated
backslashes as literal characters in combine, normalization, basename/dirname,
stem, relative paths and prefix/suffix/extension transformations. Both hosts
returned a raw backslash from `join` with one component. These are demonstrated
violations of the existing portable-separator contract, not speculative Unicode
bugs. All file and environment modes already passed on both hosts.

Commit `e9d34ef` folds separators before public path parsing and routes even the
first join component through `combine`. It does not normalize ordinary file
open arguments, alter literal POSIX backslash filenames, fold `ends_with` byte
suffixes, or emulate foreign drive/UNC/root rules. The replacement-extension
argument retains path-fragment semantics rather than becoming a validator:
separator-bearing fragments can introduce components. More edge coverage for
empty/dotted/multi-dot/separator-bearing extensions remains useful follow-up.

The independent host oracle seeds Unicode parents and filenames (ASCII,
accented Latin, Japanese, supplementary characters, NFC and NFD), mixes absolute
and relative actual file arguments, checks exact binary/text/stream bytes and
enumerates the final namespace. Distinct seed bytes, wrong-spelling aliases,
untouched sentinels and orphan-file rejection prevent a successful wrong-path
round trip from passing. Environment checks cover host-injected Unicode names
and values, listing, empty-but-present values, explicit removal and Unicode CWD
set/get/restore. The parent environment is checked unchanged. None of this
claims arbitrary unmanifested Windows-host/code-page support.

Authoritative paired evidence is retained under
`/home/nathan/vm/nil-win11/artifacts/ordinary-io-parity-20261102-jO6d2I`:

| Observation | Result | Report SHA-256 |
| --- | --- | --- |
| `linux-baseline-v2` | lexical failed twice; files/environment passed | `66b56a992382fc81b71417df9e941f9bd1c14f9fae14e0dfd82db356d83c40a5` |
| `windows-baseline-v1` | lexical failed twice; files/environment passed | `9493dc6aa56f863a32be6ee52c83f302b2c5bd5ae00d80667dfd076f67a5770c` |
| `linux-fixed-v1` | all six passed | `6b3e9f2f31b7721c87da501a3d8de517ebdf1996e9dce6c1c01ade0e500d6dfb` |
| `windows-fixed-v1` | all six passed | `9ee4a7435112f3a3e924bb5dbc6cc4e7e2c9e92dcfa3f42f822f2a7c33d75fe2` |

All four use consumer SHA-256
`d1328fd7ff9ef425f13bf56034597d2c7231f268f48f54f000ee8177d099f3c4`
and retained runner
`79ea4ad5f3efa4ee6a137deed7b0df197f926e111364bdcf55a982a005e180eb`.
The earlier Linux v1 observation is also preserved, but did not self-copy/pin
its runner; it is not the authoritative runner-bound baseline. Before/after
tool, consumer, runner, ownership helper and source-STL hashes remain part of
each report. Original failures are not rewritten by the passing fixed runs.

The independent four-run reconciliation is `ordinary-io-reconciliation.json`,
SHA-256 `529891e01cf1e8c58123fb5958da4bd195ad635d8a2e0d95f4d880d4bd674079`.
Nineteen positive/negative controls and a separate peer review pass. The
verifier rechecks retained raw logs, compiled NIL, exact consumer arguments,
copied sources and all final filenames/bytes without opening foreign absolute
paths. It confirms that the only STL delta is the same `io/path.ny` change on
both hosts. Historical tool payloads were not copied into this evidence set;
their identity/integrity and process cleanup remain measurements from the
pinned runner, not claims of independent offline binary or live-process proof.

The permanent consumer and Python-3.8-compatible harness are registered as
`nil.ordinary-io-oracle` and `nil.ordinary-io-interpreter`. Evidence directories
are fresh and retained; explicit output directories cannot be reused. There
are 23 host controls, including bounded hashing and synthetic reparse rejection.
Linux passed all 23; Windows passed 22 with the one explicit POSIX-only skip
(`windows-oracle-v1`, 4.008 seconds, owned cleanup and source identity verified).
The actual POSIX symlink control is explicitly skipped on Windows, not presented
as a real Windows junction test. `2c3e0c7` changes only the timeout control's
startup allowance from 0.1 to 2 seconds for the Windows gate plus child-Python
launch; real compiler/consumer deadlines are unchanged.

Linux full regression: 561 registered, **559 passed, zero failed**, with the
optional display and live-PostgreSQL tests skipped (194.70 seconds). After the
final comment-only rebuild, compiler/interpreter hashes match the fixed
diagnostic and all seven focused ordinary-I/O, strict byte-file Unicode,
file-stream/path and system tests pass again. Windows full regression at
`2c3e0c7` reports **564 registered, 562 passed, zero failed**, with only the
service-session display and live-PostgreSQL tests skipped (935.80 seconds).
The bounded wrapper reports successful exact-owned cleanup and unchanged
source/script inputs. Both new ordinary-I/O CTests pass. These two environment
skips are explicit build-tree evidence, not substitutes for the earlier strict
installed GTK run or the live-database gate required by SDK acceptance.

`windows-regression-reconciliation.json` independently checks the exact 564-name
registration inventory against JUnit, each run/skip status, zero failures,
reviewed phase scripts and raw log hashes. Its SHA-256 is
`63f50e0ca179f463416a7267d4567ffae8cf3b4a915264785dd8f3bfe7cea4dc`;
all 30 verifier controls pass, with independent root and peer review. The
retained JUnit SHA-256 is
`b286c38c2a20cfc60f061694652604fd72c79a93e9a55cc5231129d4cc7b86ff`.
Replaying both final reconcilers from the durable artifact copy succeeds.
The guest source is clean, build/test child cleanup is verified, saved
PowerShell policies remain Undefined, and the optional uploader remains absent
with its disabled copy retained. New SDK construction and installed acceptance
against this source are the next delivery step, without replacing the frozen
SDK or erasing its earlier failed GTK observation.

### VS Code dependency maintenance, separate from SDK acceptance

Commit `0b4789a` patches only the lockfile's `qs` 6.16.0 to 6.16.1, within the
existing transitive packaging dependency range. Linux editor type checking,
all 162 unit tests, the build, and bounded serialization regression cases pass.
The bundled extension is byte-identical
(`844aaee30df62134efa971c9bf1d3e9a1e71ca39f3665f1a3ee3d0f9e13fd500`);
`qs` and packaging tools are not shipped in the frozen VSIX. GitHub alert 7 is
confirmed fixed at `2026-11-03T02:27:19Z` by an additive sanitized API checkpoint.

The separate [minimatch advisory](https://github.com/isaacs/minimatch/security/advisories/GHSA-g3hv-75wg-m249)
remains open as GitHub alert 8. Installed/shipped `minimatch` is 10.2.6; patched
11.0.1 is outside both current parent ranges, and no patched compatible 10.x
was available. No major override or suggested language-client downgrade was
applied. Inspected runtime callers retain optimization level 1 rather than the
advisory's level-2 prerequisite, including a bounded benign options check.
That is a scoped reachability observation, not remediation or dismissal.
The pre-existing default-Node packaging CLI module-loading issue is also not
claimed fixed; no new VSIX was packaged or activated.

Dependency review and raw command logs are retained in the same artifact root's
`vscode-dependency-review`, including `review.json`
(`de09fb0cd97f8bcb62502a3858809383c32bedb1ca4edd43edf87c4deda9b152`)
and the later `github-alert-checkpoint-20261103.json`
(`2b3c20610ba41cef01ddd73a2758378842bff194f1c3beb514d306f09f22eebd`).
The frozen SDK archive and VSIX hashes were independently rechecked unchanged:
`b7a096df65caa9000c74ce1bf5249e8fc0ec221dcb9f082fd89e7948d69817f7`
and `4e6ad286ec6707131f869ea6ebd1fa4df18ae1bf40be551d6a03f4b5ad2ef3e1`.
No SDK activation, Lich publication, Anomalous modification, desktop task,
machine-policy change, model/GPU workload or long-lived server was performed.
Installed qualification of the new bytes, the earlier unexplained GTK helper
observation, bare-host/toolchain closure and the previously listed shared
language/provider limitations remain open; this is not a v1/RC declaration.

### 2026-11-03 — fresh d567 Windows development SDK and installed acceptance

Built a new, isolated Release/LLVM Windows SDK from clean, pushed source
`d567e7ed869f7853469ef9225940e1c27a6046a8`. No implementation changes were needed
for this refresh. The SDK includes the ordinary-path fixes and newly rebuilt
binary STL packages; the older SDK was neither modified nor activated over.
The subsequent notebook checkpoint does not change the SDK's source revision.

Durable delivery and evidence:
`/home/nathan/vm/nil-win11/artifacts/d567e7ed869f-parity`.
The archive is `nil-framework-windows-development-d567e7ed869f.zip`,
28,533,819 bytes, SHA-256
`18bb2aec41bae90016bb35f391dfd08ccede594285aec352b956b100c9d2ce9d`.
Its manifest SHA-256 is
`abd3f45b2863179772946f84a9b479ad80346977f324aa16f1b5fe048fc963db`.
There are 260 archive entries: the manifest plus 259 sealed payload files.
The canonical layout remains 11 root executables, `nillib` binary libraries
(18 logical modules), `nilstd` sources, and `docs` (66 HTML pages), with no
nested `bin` directory. MSVC/runtime prerequisites remain external.

The fresh guest candidate is
`C:\provision\sdk-d567e7e-20261103-9MNlNv\candidates\development-2462cd3a77654671a50f76cb8524079b\sdk`.
The whole build/test/package/consumer workflow completed in 1,556.799 seconds
inside its reviewed 7,200-second owned-process boundary. Raw output, exact
helper hashes, unchanged inputs and successful child cleanup are retained in
`pipeline-v1`; the archive/dependency step is retained separately in `archive-v1`.

| Fresh check | Result |
| --- | --- |
| Full Windows CTest | 564 registered; 563 passed; zero failures/errors; only noninteractive `nil.ui-display` skipped; live PostgreSQL passed |
| Installed consumer matrix | 1,184 executions passed: 74 fixtures, both library layouts, eight routes |
| Installed ordinary I/O with source STL | Six interpreter observations passed: lexical paths, Unicode files and environment handling in both NIL encodings |
| Installed binary-package path regression | Two lexical interpreter executions passed, one per NIL encoding |
| Installed debugger | 146 checks across 20 commands passed |
| Installed strict byte-file Unicode gate | 16 executions across 34 commands passed, using both installed library layouts |
| Fresh archive GTK desktop | All 10 installed-SDK VM/native language cases passed; lifecycle checks 235; provider assertions observed 4,471 |
| Installed-SDK VS Code integration | 88 checks, zero exclusions, 31 tasks, seven SDK tools and two debugger sessions passed |

The CTest run took 460.71 seconds. Its JUnit SHA-256 is
`21936d23923a0e26b9831ca2d7b4293860b630467759c878616ea7bc4165d4b5`.
A separate, raw read-only CTest registration capture has SHA-256
`e0144e80fa9b1afa8450c4fbb178a5ceb5874a012b8431237cb6b6fedbd79626`;
its exact 564 names match the prior independently retained inventory.
The delivery verifier checks that inventory, actual JUnit statuses, archive
and manifest anchors, canonical layout, complete committed source-STL
inventory, sealed tool/runtime bytes and the nested consumer command graphs.
Its 65 positive/negative controls and independent peer review passed.

The stock consumer report contains 148 nested six-route reports, covering 888
executions and 1,628 command records. The remaining 296 standalone
assembler/linker pipeline executions have aggregate PASS evidence only, not
retained per-command stdout/argv. Successful temporary route artifacts were
deleted by the stock runner. Offline reconciliation verifies the retained
semantic records and seals; it does not claim to replay those deleted bytes
or reconstruct original nested JSON serialization hashes.

Installed ordinary-I/O evidence is retained as `installed-source-io-v1` and
`installed-binary-lexical-v1`; report SHA-256 values are respectively
`ca1b0461fd1393acc909320acd0d4fb8372f9aa61fea1ad90686528b4845f976`
and `5d59defbb39e72a36b66e6044de2f20763d55e1dcdabd4fec03702099ad4a105`.
The latter binds the former's installed input identities and raw outputs to
the independently anchored SDK seal. That binding is not an independent
namespace-oracle replay. Both source and binary library selection are
explicit, with no implicit STL fallback. Binary NIL packages contain embedded
Ny source; this is binary-package delivery evidence, not source-free
compilation or native ordinary-I/O support.

The strict desktop run extracted the new archive into a fresh directory and
used a one-off Limited/Interactive task for the already signed-in Nathan SID
ending in `1002`. Both interaction routes retain their complete 27-event,
verified Job-owned HWND traces; no global input or focus change was used.
The provider count is timing-dependent polling evidence, not a fixed test
inventory. The 235 lifecycle checks are exact. Before/after build-provider
captures are identical, all extracted SDK payloads remain sealed, the task
was removed, and controller and inner-process cleanup were separately verified.
Its UI report SHA-256 is
`a72ebf2ddbaf839184006096ffe5e76f1a329e3260743f18fe18ee6942510051`.
The old zero-exit/empty-output GTK observation is still preserved and its
cause remains unresolved; this distinct passing run does not erase or explain it.

VS Code used the unchanged frozen extension bundle/VSIX and isolated profile,
with the new SDK and current editor-source inventory independently bound.
All 17 shipped VSIX assets still match the corresponding current sources.
The whole owned editor workflow completed in 145.018 seconds within its
800-second boundary. Preflight records, including both build-helper EXEs,
match exactly before and after. No new VSIX was packaged and no user editor
configuration was activated. The previously recorded packaging dependency
and release limitations are not declared resolved by this run.

Independent host reconciliations, with raw evidence and reviewed private
helpers retained alongside them:

| Report | SHA-256 |
| --- | --- |
| `delivery-reconciliation.json` | `6cefe2d379a261af05580c81fd869c6f0fa42a283f7ab167a7043880fbd411c7` |
| `tooling-reconciliation.json` | `9e2af8fe7908c050ced117921409584a2c660cf8ad7d587839e4e6f3309235a2` |
| `desktop-reconciliation.json` | `c825b83481aca8520a3d2ebbdc21ccb13af5e9dc41433172fb1880605d6b610a` |
| `editor-reconciliation.json` | `58378ee1334cfdd934ed70595fcb122f6e86d332b45bba7a851262b9bb6e774b` |

The retained-copy delivery verifier also reproduces the primary reconciliation
byte-for-byte. Debugger/Unicode, desktop and editor replays bind to this same
manifest/archive; their individual narrower scopes remain explicit.
The frozen previous SDK archive and VSIX were rechecked unchanged at
`b7a096df65caa9000c74ce1bf5249e8fc0ec221dcb9f082fd89e7948d69817f7`
and `4e6ad286ec6707131f869ea6ebd1fa4df18ae1bf40be551d6a03f4b5ad2ef3e1`.
Source remained clean at d567 throughout acceptance. Post-run checks found no
remaining task from this desktop run; saved PowerShell policies remain
Undefined and the optional telemetry uploader remains disabled with its
disabled copy retained. The read-only guest receipt is
`final-state-before-notebook-sync.json`, SHA-256
`f9cf160df69210eb6d0f6c5be331a1656ee61a8d12829f83259573fd6d09cfeb`.
Process-scoped RemoteSigned was used only where
already approved. No SDK activation, Lich publication, Anomalous changes,
local-model/GPU workload or long-lived development server was performed.

This is a fresh, verified Windows **development** SDK checkpoint, not a v1/RC,
unrestricted parity or bare-host portability declaration. Bare-host/toolchain
qualification, the original GTK anomaly, and the previously recorded shared
language/provider limitations remain open. Native ordinary I/O is still a
shared unsupported route, not made supported by these interpreter checks.

### 2026-11-03 — shared native ordinary I/O and real execution checkpoints

The follow-up audit confirmed two shared implementation gaps, not merely
Windows omissions: ordinary I/O worked in the interpreter but lacked native
lowering, and the AI client's reload/snapshot commands returned unsupported.
The existing AI pause path also changed unsynchronized debug state without
actually stopping ordinary execution. Historical command names and separate
FFI DLL reload experiments were not evidence of execution save/restore.

Added exact-ABI native lowering/runtime support for ordinary file text/bytes,
copy/move/delete/size/exists, lexical paths, checked byte streams and scalar
environment/working-directory/host-identity operations. Native stream tokens
are context-owned, bounds-checked and never host descriptors. The Unicode
consumer exercises both NIL encodings, both bundles, both standalone
assembler/linker routes, JIT and AOT, with independent exact namespace and raw
byte checks including NFC/NFD distinctions. Incorrect intrinsic declarations
are rejected before producing an executable. The existing stream and path/file
route fixtures now include native execution.

Native `environment.list` remains unsupported because named-struct-array
lowering is a separate gap. Native text remains NUL-free; `read_text` rejects
NUL-bearing content explicitly and directs callers to the byte API. Ordinary
file operations retain their normal link-following/overwrite behavior and do
not acquire the secure-byte API's stricter filesystem guarantees.

The user selected full execution capture/restore, and explicitly approved
same-process restoration as the first implementation. The shared VM now has
owner-thread instruction-boundary callbacks, noncopying bounded inspection,
and opaque reusable checkpoints of PC, real frames/locals/globals, operand
stack, constants, heap/alias identities, ownership/fault/GC state, arguments
and execution settings. Restore validates and clones before publishing any
state. Checkpoints are bound to one VM and loaded-program generation, with
16 MiB logical storage, 65,536 value-visit and depth-64 limits. Independent
review caught and fixed traversal-order-dependent shared-subtree depth checking.

This initial checkpoint profile is deliberately restricted: whole programs
containing interop, console/time/native/parallel work or builtin/dynamic calls
are rejected, as are boxed/foreign values, cyclic aggregate graphs, open file
handles and memory profiling. Host callbacks, external resources and emitted
diagnostics are not rewound. It is not general application rollback, disk
persistence, native/JIT process rewind or code hot reload. `vm.reload` still
reports unsupported. The exact contract is in `docs/design/NIL-0006-ai-module.md`.

The local AI endpoint now uses strict bounded JSON, owner-only Unix sockets /
Windows named pipes and joined nonblocking transport. Commands execute on the
VM owner thread, including real pause/resume, step, inspection, capture and
restore. `--ai-wait` starts paused before main. Capability-negotiated nilagent
commands expose the same semantics. Timeout/rejection never becomes a success
placeholder. Review also addressed oversized error diagnostics, invalid
inspection metadata, unnecessary large-value copies and final-reply delivery
during fast program shutdown. No ETP migration is implied by these JSON-v1
changes; that remains a separately versioned integration.

Initial full Linux regression: 567 registered, 565 passed, zero failures,
with only optional display/live-PostgreSQL skips (104.07 seconds). VM checkpoint
tests passed 210 checks and control tests 237 checks. The final transport adds
a delayed-reader shutdown regression (57 Linux checks). The private evidence
root is `/tmp/nil-control-io-20261103.VGX6d6`; the repeat full Linux run after that
last transport change again passed 565/567 with the same two skips and zero
failures (101.62 seconds), retained as `linux-ctest-v2.xml`. Windows source-tree
verification is the next gate; Linux results are not Windows qualification.

Retained ordinary-I/O development evidence includes
`/tmp/nil-ordinary-io-parity-ps6kqy9c/report.json` (eight routes) and the subsequent
full-suite rerun with the updated STL comment. Earlier fixture/harness failures
remain retained, rather than overwritten. The GTK backend is unchanged: it
already uses GTK's Win32 backend on Windows. The old missing helper output does
not establish a rendering defect, and its original cause remains unresolved.

No previous SDK/VSIX was replaced or activated, no Lich publication or Anomalous
change occurred, and no local-model/GPU workload or development server was run.
This is shared implementation progress with explicit limits, not a v1 release
or an unrestricted checkpoint/parity declaration.

#### Windows source regression and follow-up fixes

Committed/pushed the shared implementation as `5fcb685`. The first full Windows
source build succeeded; CTest registered 570 tests: 565 passed, four failed and
the optional interactive display test skipped (523.22 seconds). The private
owned process wrapper verified cleanup and unchanged pinned source/helper inputs.
All eight ordinary-I/O routes passed their independent host namespace/byte
oracle. VM checkpoint tests passed 210 checks and owner-control tests 237 checks.

The four failures are retained in
`/home/nathan/vm/nil-win11/artifacts/control-io-20261103-VGX6d6/windows-v1/`:

- AI transport: closed during a large response; the live checkpoint client also
  timed out sending a large invalid-field request. Both used writes larger than
  the nonblocking pipe buffer. Follow-up bounds Windows write chunks to 4 KiB
  without increasing frame/operation deadlines, and adds transport stage diagnostics.
- SDK preflight: the private runner omitted the previously approved inherited
  process-scoped `RemoteSigned` setting. The retry runner sets it only within
  its `setlocal` build/test environment; saved machine/user policies stay unchanged.
- Tuple-array NIL contract: Windows refused replacement of a repeatedly reused
  `bounds.exe`. Each schema/bounds case and encoding now uses separate native
  artifacts; assertions and production linker behavior remain unchanged. The
  exact owner of the original publication lock is unproven. Separate linker
  publication/preservation tests remain required; this is not a linker fix claim.

Independent review also found that explicit Ny process exit in AI mode could
bypass control cleanup. AI mode now traps the exit through the existing VM
mechanism, returns the requested code, finalizes profiling and joins/removes its
control endpoint. Real-process tests cover connected and immediate exits, plus
completed memory-profile output. Unix transport compilation no longer assumes
Linux-only APIs on Apple/BSD; those hosts are not runtime-qualified by this work.

An intermediate full Linux rerun passed 565/567 with the same two optional
skips and zero failures (109.00 seconds), retained as `linux-ctest-v3.xml` with
SHA-256 `df82425a1ce9f2269e60b463d6f13172df0a621c0336768be2fb6386c613bf73`.
The Windows follow-up must pass focused and full regression before being
considered verified. No SDK has been rebuilt/activated or published in this slice.

#### Verified shared-source checkpoint at `5307098`

Committed/pushed the follow-up as `5307098fc7f1cafe07c5903931ea9ffe0360238c`.
Windows was cleanly fast-forwarded from `5fcb685`; the focused rerun passed all
four previously failing tests, including the 65,536-byte transport echo and
delayed-reader final reply after server shutdown. The complete final runs were:

| Host | Registered | Passed | Failed | Skipped | CTest wall time |
| --- | ---: | ---: | ---: | ---: | ---: |
| Linux | 567 | 565 | 0 | 2 | 105.72 s |
| Windows | 570 | 569 | 0 | 1 | 496.95 s |

Linux skips were the optional display and live PostgreSQL tests. Windows only
skipped the interactive display test; its isolated live PostgreSQL check passed
all six routes and verified graceful shutdown/owned-process cleanup. No desktop
GTK acceptance was repeated here, and its earlier recorded limitation remains.

Both hosts passed the full eight-route ordinary-I/O oracle using source STL,
with exact output namespaces/bytes and unchanged tools, fixtures and libraries.
Both also passed 210 VM checkpoint checks, 237 owner-control checks and the
live capture/mutate/repeated-restore/continuation/rejection/explicit-exit contract.
Transport passed 57 Linux checks and 48 Windows checks (platform-specific counts).
The isolated tuple-array contract retained all 126 checks. Separate linker
publication/preservation checks passed; the original transient lock owner remains
unproven rather than being attributed to a scanner or hidden Ny process.

Final durable evidence is under
`/home/nathan/vm/nil-win11/artifacts/control-io-20261103-VGX6d6/`:
`linux-ctest-v4.xml`, `linux-native-io-v4/`, `windows-focused-v2/` and
`windows-v2/` (including the complete Windows eight-route oracle). Earlier
failure reports and helpers remain preserved. Final JUnit SHA-256 values:

- Linux: `dccacd9bf11c5fc2d7a85d41d9af07acbe7169dfdb8ae32740926dd0b9f5f79f`
- Windows: `30db91e40762f79cbd5e96329915c4ade48a39c8204fd805e861d1a2fd88da25`

The Windows wrapper reconciled all registered test names against JUnit, rejected
unexpected failures/skips and verified owned-process cleanup plus immutable
source/helper inputs. Post-run inspection found no remaining compiler,
interpreter, assembler, linker, CMake or CTest processes. All saved PowerShell
policy scopes remain Undefined; the optional MSVC uploader remains absent with
its reversible disabled copy present.

This verifies the shared source implementation, not a new installed SDK or v1
release. SDK repackaging and installed acceptance remain next. Checkpoints still
require the documented deterministic same-process profile; external-resource
rollback, code hot reload and native `environment.list` remain unsupported.
No previous SDK/VSIX activation, Lich publication or Anomalous change occurred.

#### Fresh Linux and Windows installed SDK acceptance at `d5e8758`

Built isolated development SDK candidates from clean
`d5e875879b05cfee5c1a44cb5f94da22db5e1fba` (the verified shared implementation
plus the preceding notebook checkpoint). Both pipelines captured fresh build
qualification before fresh CTest, then installed, compiled documentation,
packaged/strictly checked the STL, sealed/validated the SDK and ran installed
consumers. The Linux staging helper deliberately did not invoke the activating
`build-sdk.sh`/`publish-sdk.py` path. Windows used `build-sdk.ps1` with a new
candidate parent, four build workers and the already-approved process-scoped
RemoteSigned environment. No implementation change was needed in this slice.

| Fresh SDK gate | Linux | Windows |
| --- | ---: | ---: |
| Registered CTest cases | 567 | 570 |
| Passed / failed / skipped | 565 / 0 / 2 | 569 / 0 / 1 |
| Installed consumer executions | 1,184 | 1,184 |
| Ordinary-I/O executions, source + binary STL | 16 | 16 |
| Unicode byte-file executions, source + binary STL | 16 | 16 |
| Installed debugger checks | 146 | 146 |
| Installed interpreter live checkpoint contract | PASS | PASS |

The CTest skips remain Linux display/live PostgreSQL and Windows display only;
Windows live PostgreSQL passed. Linux CTest took 112.80 seconds; Windows JUnit
records 427 seconds. The whole owned Windows SDK pipeline took 1,523.29 seconds.
Each SDK contains 11 root executables, 18 strictly checked logical STL modules,
source under `nilstd`, compiled packages/native runtime under `nillib`, and 66
HTML documentation pages. Each seal contains 259 payload files plus its manifest.

The dedicated installed ordinary-I/O gate checks all eight routes against both
STL layouts, with 36 exact command records, two intentional invalid-ABI
rejections, retained native artifacts and independent raw-byte/Unicode namespace
oracles. Checkpoint acceptance runs once against the installed interpreter with
raw NIL fixtures; it is not an STL-layout test. It covers capture, mutation,
reusable restore, continuation, rejection and explicit-exit cleanup. Both SDKs
also passed debugger JSONL/DAP consumer validation (146 checks, 20 commands) and
the separate strict byte-file Unicode contract (16 executions, 34 commands).
Windows tooling evidence was independently reconciled again after transfer.

Durable evidence and delivery files are under
`/home/nathan/vm/nil-win11/artifacts/d5e875879b05-parity/`:

- `linux-delivery/nil-framework-linux-development-d5e875879b05.tar.gz`
  (38,094,488 bytes), SHA-256
  `7448194160221a1328f5b153880c8672a9b0de7556aff0cb9b2866cb6c660201`.
- `windows-delivery/nil-framework-windows-development-d5e875879b05.zip`
  (28,782,854 bytes), SHA-256
  `947368825c6ec7b439b4815bf9311c00c35af26bf6c745ae5e34b3cb39aa292d`.
- Linux manifest SHA-256:
  `cfd9b9bd30cc3575787f22c21a13cb87dfabd81d6c923838ec40705b6144407c`.
- Windows manifest SHA-256:
  `5eb104a0d2cb665e857a5f9e1dfca3654cdfc1bb7e94ed1fd2c180795d897efb`.

The Linux archive was reopened and checked against every sealed file and its
observed Unix file permissions. The transferred Windows archive passed the
independent delivery verifier against external archive/manifest hashes and a
fresh 570-name CTest registration capture. It reconciles 888 detailed consumer
execution records / 1,628 command records, plus 296 aggregate-only standalone
object executions. Successful consumer artifacts deleted by the stock harness
are not replayed. Unicode's successful temporary files are likewise deleted by
its harness; the retained namespace/command reports are reconciled, not those
deleted bytes. Checkpoint temporary fixtures are not retained. Debugger work,
ordinary-I/O artifacts, raw logs, ownership reports and private helpers are saved.
The six private-helper synthetic suites passed 217 controls with verified cleanup
and unchanged input hashes; their logs are in `helper-controls`.

All owned SDK/acceptance processes completed with verified cleanup and unchanged
source/helper/SDK seals. Post-run Windows inspection found no remaining framework
compiler/interpreter/assembler/linker/debugger, CMake, CTest or uploader process;
all PowerShell policy scopes were still Undefined. The optional MSVC uploader
remains absent and its reversible disabled copy remains present. The previous
`d567e7ed869f` Windows ZIP and `45e7b9a` VSIX were rehashed unchanged.

These are development SDK candidates, not a v1 release or unrestricted parity
declaration. No active SDK or VSIX was replaced, no desktop GTK/editor acceptance
was repeated, and no Lich publication, Anomalous change, local-model/GPU workload
or development server occurred. External runtime/toolchain prerequisites remain
required; PE import inventory is not bare-host qualification. Checkpoints retain
the documented deterministic same-process restrictions; arbitrary external-state
rollback, code hot reload and native `environment.list` remain unsupported.

#### `d5e8758` desktop acceptance and editor failures, November 3

The unchanged Windows SDK was extracted into Nathan's desktop-owned evidence
directory and checked against its independent archive and manifest anchors.
The strict GTK run passed all ten installed language cases across VM/native
routes, observed 4,471 provider assertions and passed 235 lifecycle assertions.
The provider total includes timing-dependent polling assertions, so it is an
observed count rather than a fixed inventory. Targeted messages went only to
verified Job-owned HWNDs; scheduled-task removal and owned-process cleanup were
verified. The separately pinned checkout was `c857e06`, whose difference from
the packaged `d5e8758` source was the preceding notebook update.

Editor attempt v1 failed while reading the original candidate's
`nillib/collections.nil`. Retained ACL evidence shows that file belonged to
Administrators and did not grant the limited Nathan desktop account access.
The retry used the existing, independently verified Nathan-owned extraction;
the original SDK's ACLs were not relaxed. Both copies of the library retained
the same hash. This addressed test provisioning, without changing SDK bytes.

Editor attempt v2 completed 83 checks, then failed because memory profiling
could not return a saved report URI. A bounded comparison using the same SDK
confirmed successful short-path output, failure at an ordinary Unicode path of
270 UTF-16 units, and success using an extended Windows path prefix. The SDK
seal remained unchanged; these are failure/diagnostic records, not editor PASS.
Both editor attempts retained verified owned cleanup.

Evidence is under
`/home/nathan/vm/nil-win11/artifacts/d5e875879b05-desktop-editor/`:
`desktop-reconciliation.json`, `editor-v1-sdk-access.json`,
`editor-v1/`, `editor-v2/`, `editor-v2-host-report.json`, and
`path-diagnostic/evidence-8cf1de96f8314951a9e4b15ebaee6226/`.

#### Aperture long report paths: `164f179`

The fix normalizes Windows report paths locally before using the extended path
namespace where needed. Creation, profile readback and failed-output removal
use the same resolved path. Exclusive creation, existing-report preservation,
bounded reads and failure cleanup remain required; no machine-wide long-path
setting was changed. Regression cases exercise ordinary Unicode paths beyond
260 UTF-16 units, rather than requiring callers to supply a Windows prefix.

A focused Windows Aperture rebuild passed `aperture.memory-contract` with
151 checks. Its owned wrapper verified clean source, unchanged inputs, process
cleanup and the unchanged older packaged SDK. Durable evidence is in
`d5e875879b05-desktop-editor/path-validation/evidence-6c65edc6ffb64c598e9e0c7bf8d8d01b/`
under the artifact root above; JUnit records 22.05 seconds for the contract.

Earlier work also reported a successful Linux translation-unit compile with
`-Wall -Wextra -Werror` and 139 checks from the updated Aperture contract against
the unchanged `d5e8758` Linux SDK. Their raw logs were not recovered after the
host restart. These are prior reported compatibility/compile checks, not a
rebuilt Linux SDK, a new full Linux suite or retained installed acceptance for
`164f179`/`f4d5d04`.

#### Full Windows SDK attempts and startup diagnostic, November 3–4

The November 3 `164f179` pipeline was interrupted by the host/VM shutdown. Its
retained stdout ends after 187 of 570 registered tests; no completed pipeline
or SDK acceptance can be inferred. The incomplete source/candidate evidence
was preserved under `164f1797f2d0-parity/interrupted-20261103/`.

The fresh November 4 attempt completed CTest with 567 passed, two failed and
one skipped out of 570 registered tests (775.66 seconds). Only the interactive
display test was skipped. Aperture passed its 151-check contract. The two
failures were:

- `niltest.process-contract`: a Windows `PermissionError` while opening an
  atomically published cancellation-child identity file.
- `ny.repository-policy`: the source scanner entered retained generated
  debugger work and tried to read an inaccessible `fixture-native.exe`.

The pipeline failed before SDK installation and packaging, preserved its candidate, and
verified owned cleanup plus unchanged pinned inputs. Raw logs and its FAIL
report are retained in `164f1797f2d0-parity/failed-pipeline-20261104/`.

Some generated native executable launches also appeared much slower than
compiler/interpreter commands. A separate 300-second/2-MiB owned diagnostic
compiled a harmless C++ program printing `42` and timed `Popen` separately from
`communicate`. The first grouped launch spent 8.581 seconds in `Popen` and
0.005 seconds after startup. Its second grouped launch and both subsequent
ordinary launches started in roughly 0.002 seconds; the existing compiler
baseline started in 0.004 seconds. The five observations passed with cleanup
verified and unchanged inputs. They establish one delayed first startup,
without attributing it to antivirus, Windows policy or console allocation;
the launch order also prevents a cold-start comparison between flag choices.
Evidence is in `164f1797f2d0-parity/spawn-diagnostic/evidence-3e302c9087e241b2b6d89ce9a6e3c57a/`.

#### Harness repairs and focused acceptance: `f4d5d04`

The PID producer already publishes complete identity JSON through a closed
temporary file and atomic rename. The Windows reader now includes
`FILE_SHARE_DELETE`, allowing access while a rename handle still exists. The
original ten-second publication deadline and PID/creation-time checks remain;
real access errors and malformed published records remain failures. A
deterministic Windows control holds DELETE access, requires the old CRT read
to fail, and requires the corrected read to succeed before that handle closes.
A second control verifies that malformed evidence cannot bypass cleanup of
already-known children. This reproduces a compatible race mechanism; the owner
of the original failing handle was not identified.

Repository policy now prunes the four declared generated Ny/Matrix 3D
`work`/`reports` trees before descending, while preserving existing build-tree
exclusions. Similarly named source directories and readable untracked sources
remain in scope. Source traversal, metadata and read failures remain errors.
Seven controls cover those boundaries; their new CTest registration brings the
expected Windows inventory to 571.

Local Linux checks passed all 39 niltest process invocations and seven policy
controls. The process contract used the unchanged `d5e8758` Linux niltest binary;
this did not rebuild or requalify a Linux SDK. Windows focused validation then
passed all three selected tests: repository policy, its seven controls and the
39-invocation process contract. The first private focused wrapper incorrectly
failed because CTest's 1,024-byte successful-output cap omitted the policy
summary, although CTest itself passed 3/3. That FAIL report remains preserved;
the separate v2 run raised only that report-output cap to 16,384 bytes and
retained complete evidence, unchanged inputs and verified cleanup.
See `f4d5d045c76e-parity/focused/` and `focused-v2/`.

The committed and pushed `f4d5d04` source remained frozen for the complete SDK
and GTK runs below. `origin/master` and the local source checkpoint agree.
The draft stayed outside the repository to preserve exact clean-source pins
through SDK, desktop and editor acceptance.

#### Fresh Windows development SDK and independent delivery: `f4d5d04`

The complete clean-source Windows pipeline passed all executed CTest cases:
571 registered, 570 passed, zero failures/errors and one skipped service UI
case. The live PostgreSQL test passed. Retained JUnit output confirms the
151-check Aperture memory contract, 39 niltest process invocations, clean
repository policy and all seven named policy-control successes. CTest's default
1,024-byte cap truncates only the policy-control summary after those seven
successes; reconciliation requires the exact observed marker and successful
CTest status rather than inventing an absent summary.

Installation, standard SDK validation and the installed consumer route gate
passed. The package seals 259 payload files, including eleven tools at the SDK
root, eighteen compiled standard-library modules in `nillib`, complete `nilstd`
sources and 66 HTML documentation pages. The ZIP has 260 entries including its
manifest. The fresh candidate is
`C:\provision\sdk-f4d5d04-20261104-icD0Xv\candidates\development-9d2db0d193f744629774794a3fe992e9`.

The consumer gate records 74 fixtures across source/binary STL layouts and eight
routes, totaling 1,184 executions. Host reconciliation checks 148 nested reports,
888 detailed execution records and 1,628 retained command records. The remaining
296 standalone-object executions retain aggregate evidence only; successful
temporary artifacts were deleted by the existing runner, so their bytes cannot
be independently replayed. These evidence limits are unchanged.

An independently anchored host verifier accepted the archive, manifest, fresh
571-name registration, payload layout, STL source inventory, documentation,
CTest outcomes, consumer records and twelve PE dependency records. The latter
inventory covers eleven tools and the shared runtime; external MSVC/runtime
prerequisites remain required. A separate host owner audit reconciled the
pipeline logs, six archive commands, 77 retained inventory entries, source/helper
pins and SDK seals. Both owned runs reported bounded success, unchanged inputs
and verified cleanup. No source or SDK bytes changed during archival.

Evidence is under
`/home/nathan/vm/nil-win11/artifacts/f4d5d045c76e-parity/`:

| Evidence | Result |
| --- | --- |
| `pipeline-v1/report.json` | Full SDK pipeline PASS; raw stdout/stderr retained |
| `archive-owner-v1/report.json` | Archive creation PASS; retained inputs and raw command logs |
| `windows-delivery/independent-reconciliation.json` | Independent delivery PASS; digest starts `38ab96217517` |
| `independent-owner-reconciliation.json` | Owner/input/log reconciliation PASS; digest starts `6fca9c2df362` |

The earlier `164f179` interruption, failed full pipeline, failed focused wrapper,
and `d5e8758` editor failures remain at their original evidence paths. The fresh
PASS does not alter those records or explain the original delayed launch or
empty lifecycle-output observation.

#### Fresh installed GTK acceptance: `f4d5d04`

The strict desktop gate extracted the new archive into a fresh Nathan-owned
directory and verified all 259 payload files against the same archive/manifest
anchors accepted above. Its checkout and build-tree provider source were both
pinned to `f4d5d04`; no notebook-only checkout exception was needed.

All ten installed GTK language cases passed across VM/native routes. The
separately pinned build-tree provider emitted 4,459 observed assertions, and the
lifecycle helper emitted its required exact 235-check summary. Provider polling
counts vary with timing and are not a fixed test inventory. Input used
`PostMessageW` only for verified Job-owned HWNDs. The limited Nathan scheduled
task was removed, owned cleanup was verified, and source/provider/helper inputs
and the extracted SDK remained unchanged. This provides a separate interactive
gate for the UI test skipped by the noninteractive full suite.

The guest evidence is retained under
`desktop/evidence-1311b89c24654c478cd764ba9b3103fc/` in the artifact root above.
`desktop/desktop-owner-v1/report.json` records the owned run;
`desktop-reconciliation.json` records independent host replay PASS (digest starts
`34e38378fe1c`). The original unexplained zero-exit/empty-output lifecycle record
remains preserved and explicitly unresolved by this distinct strict run.

#### Editor acceptance: first fresh attempt failed

The first fresh editor attempt used this exact accepted archive and Nathan-owned
extraction. It completed 31 checks and recorded 23 tasks, then failed with
`Timed out: current compiler check` in `vscode/test/project-checks.cjs`. The
existing whole-run deadline was not exhausted. The editor owner separately
reported verified Job cleanup and unchanged inputs. Post-run inspection found the
scheduled task removed and inactive, although an early-return bug in the
private controller prevented those cleanup facts from reaching its aggregate
flags after acceptance failed. A separate controller proposal now records cleanup
facts independently of acceptance and passed its pure controls and peer review;
the original reports and acceptance FAIL remain preserved.

Evidence is retained in `editor-v1/`, `editor-controller-v1/` and
`editor-v1-host-report.json` beneath the `f4d5d045c76e-parity` artifact root.
Complete editor acceptance, including all four Ny/Matrix VM/native profile
reports at ordinary Unicode paths exceeding 260 UTF-16 units, remains pending.
The older 83-check partial editor run is also still a failure record, not
acceptance for the repaired SDK. The independent core SDK and GTK PASS results
remain valid for their stated scopes. No active SDK or VSIX, Lich release or
Anomalous checkout has been changed.

#### Editor analysis import-search bottleneck and focused fix, November 4

Bounded raw probes reproduced slow successful full-graph analysis with both
surfaces. Minimal Matrix analysis completed in 55–78 ms, while overlaying all
35 SDK dependency sources still left full-graph analysis around eight seconds.
Removing the optional semantic index did not remove that cost. The production
editor timeout was unchanged.

A separate compiler copied from the pinned source added 22 aggregate phase
metrics; it did not replace the SDK or build-tree compiler. Exact source-graph
comparison confirmed the same 36 analyzed sources, with input hashes and owned
cleanup verified. The private metrics measured:

| Private Windows analysis | Import resolution | Whole compiler analysis |
| --- | ---: | ---: |
| Matrix | 7.423 s | 8.725 s |
| Ny | 7.036 s | 7.583 s |

Each run made 62 import-resolution calls for 35 distinct targets, probing 11,801
source candidates with 11,739 workspace-probe misses. Nested phase times must
not be added together. The first Matrix run's external wall time was 12.047 s,
including 3.241 s before `execFile` returned; that launch delay is separate from
the measured compiler phases. These observations do not identify a Windows
policy or antivirus cause. Retained private build/probe evidence is in
`f4d5d045c76e-parity/editor-analysis-timing/`, including
`analysis-evidence/report.json` and raw compiler stdout/stderr.

The focused source fix remembers the first successful source resolution for an
exact import target within one compilation. Failed probes and binary-library
loads retain their existing behavior. Source and binary search roots remove
only identical supplied path spellings, retaining the first occurrence; no
canonicalization, case folding or `..` collapse changes root precedence.
Canonical source reads, snapshot consistency and toolchain fingerprints remain
in place. The cache and root list do not persist between compilations.

Behavior regressions cover a diamond import graph sharing one dirty canonical
leaf, a changed overlay followed by changed disk contents in fresh requests,
and repeated search roots preserving the earlier source over a later overlay.
Existing alias, nonexistent-directory, cyclic-link and source-before-binary
cases remain. The rebuilt Linux compiler passed all six focused CTests in
2.54 seconds: analysis, semantic index, completion, member completion,
signature and analysis dependencies. The new fixtures also passed against the
unchanged older Linux SDK; that compatibility run is distinct from the rebuilt
compiler checks and does not constitute a new Linux SDK acceptance.

Windows rebuild/measurement and complete installed editor acceptance are still
pending for this fix. The accepted `f4d5d04` SDK and GTK evidence remain attached
to their original bytes. No release/parity declaration, activation or publication
is implied by these focused results.


#### Import reuse validation and another publication race, November 4

Commit `262ceda` was pushed before the Windows rebuild. All six focused Windows
analysis tests passed (10 seconds total), with owned cleanup and unchanged
inputs. The repeated raw Windows requests completed in 1.352–1.520 seconds,
compared with 8.372–9.333 seconds before the fix. Each returned success, empty
stderr and identical analysis JSON except `compilerFiles` and `nativeRuntime`,
which reflect the build-tree compiler location. The original SDK stayed sealed.
The Linux counterparts took 0.229 seconds for Ny and 0.208 seconds for Matrix,
previously 0.703 and 0.753 seconds. These are retained single-run comparisons,
not statistical benchmarks or installed-editor acceptance.

The complete Linux build/test run registered 568 tests: 565 passed, zero failed
and three skipped (two PostgreSQL prerequisites and interactive display), in
388.31 seconds. Evidence is under
`/home/nathan/vm/nil-win11/artifacts/import-resolution-20261104/`, including
`linux-ctest-262ceda.xml` and `linux-analysis-timing/report.json`. Windows focused
and comparison evidence is under `262ceda02cc5d-parity/focused/` and
`262ceda02cc5d-parity/analysis/comparison.json` beneath the same artifact root.

The fresh Windows SDK pipeline then failed `nilbuild.incremental-contract`
while polling an atomically renamed event record. Python's ordinary text open
raised `PermissionError` at `recorded_events()` during the concurrent-driver
case. This is the same Windows sharing constraint previously addressed for
PID publications: the new name can become visible while the rename's DELETE
handle remains open. The running pipeline and its checkout were left intact
to preserve the complete failure evidence.

A shared test helper now opens published records with read/write/delete sharing
on Windows, retains bounded strict UTF-8 reads, and closes transferred handles
on every path. Both the event and PID readers use it; malformed JSON/schema,
oversized data and missing records still fail visibly. The event regression
holds a DELETE-access handle, proves the old CRT reader fails, and requires
the new reader to succeed before that handle closes. It also checks invalid
bytes/schema and missing records. No retry hides a corrupt publication.

Both updated Linux contracts pass (25.79 seconds). Copied Windows event and PID
publication controls pass under the existing owned-process boundary with
unchanged inputs. Their retained report is
`262ceda02cc5d-parity/publication-controls-v2/report.json`; the first private
wrapper's missing test-import path failure remains separately preserved in
`publication-controls-v1/`. These controls do not replace the pending complete
Windows test/SDK and desktop/editor runs.

#### Complete Windows development SDK and desktop acceptance: `5630388`

The committed and pushed shared-publication repair passed both focused Windows
contracts: 73 incremental-build checks and 39 niltest process invocations, in
41 seconds. The fresh full SDK pipeline then passed 570 of 571 registered tests
with zero failures and the expected noninteractive desktop skip, in 437 seconds.
The complete owned pipeline took 1,428.11 seconds; input seals and cleanup passed.
Installation, SDK validation and 1,184 installed consumer executions passed.
The 74 fixtures cover both source/binary STL layouts and eight routes. As before,
888 executions have detailed retained records and 296 standalone-object
executions have aggregate evidence; successful temporary route artifacts were
removed by the existing runner. No additional byte-level replay is claimed for
those removed artifacts.

The package contains eleven root tools, eighteen compiled STL modules in
`nillib`, standard-library sources in `nilstd`, 66 HTML documentation pages and
259 sealed payload files. Its archive and manifest are:

| Artifact | SHA-256 |
| --- | --- |
| `nil-framework-windows-development-563038813c7e.zip` (28,785,334 bytes) | `a4ac47169c3e1d5ec5b6e3a2e6099805fb96955dbc0ee5779f37af5bcf3bd946` |
| `manifest.json` | `cf6978a9461aa6b4b13bb00c0b960404ef0362f588b5429e6c50edb6ad58ba0a` |

The original candidate is
`C:\provision\sdk-5630388-20261104-icD0Xv\candidates\development-bb16d0944997496f91d6b4ec79dc7dda\sdk`.
Archive, delivery and owner reconciliation all passed independently on the host,
including complete payload seals, registered test results, retained consumer
records, helper/source hashes and raw pipeline/archive logs. External MSVC/runtime
prerequisites remain required. This is not a bare-host acceptance claim.

Fresh GTK acceptance extracted this archive into a Nathan-owned directory and
passed all ten installed interpreter/native cases. The separate build-tree
provider emitted 4,472 observed assertions; this polling-dependent count is not
a fixed inventory. The lifecycle helper passed exactly 235 checks. Input remained
limited to verified Job-owned HWNDs. Separate child cleanup, scheduled-task
removal, source/provider seals and unchanged SDK bytes passed host replay.

The installed editor gate then passed all 88 checks, with zero exclusions,
31 observed tasks using seven SDK tools and two debugger sessions. All four
Ny/Matrix interpreter/native memory-profile tasks succeeded at distinct ordinary
absolute Unicode paths of 267 UTF-16 units. Completion, semantic editing,
project/compiler checks, debugging and profiling ran through the frozen editor
bundle; the production compiler-check timeout was not increased. The editor
owner completed in 112.82 seconds and the separate controller in 117.42 seconds.
Both Jobs cleaned up, the Limited/Interactive Nathan task was inactive and
removed, and source/helper/SDK seals remained unchanged.

Independent host replay reconciled the editor reports against 78 source files,
17 VSIX assets and the complete accepted SDK archive. The unchanged VSIX has
SHA-256 `4e6ad286ec6707131f869ea6ebd1fa4df18ae1bf40be551d6a03f4b5ad2ef3e1`;
its tested bundle has SHA-256
`844aaee30df62134efa971c9bf1d3e9a1e71ca39f3665f1a3ee3d0f9e13fd500`.
The frozen editor-only verifier's original `PENDING` archive field is retained;
the separate delivery replay provides the successful archive binding. No new
VSIX was built or activated.

Evidence is retained under
`/home/nathan/vm/nil-win11/artifacts/563038813c7e-parity/`:

| Evidence | Result / SHA-256 prefix |
| --- | --- |
| `pipeline-v1/report.json` | Complete SDK pipeline PASS |
| `windows-delivery/independent-reconciliation.json` | Delivery PASS; `860fcd1e1f70` |
| `independent-owner-reconciliation.json` | SDK/archive owner PASS; `1b19b736ecaf` |
| `desktop-reconciliation.json` | GTK host replay PASS; `8476e69e3cb7` |
| `editor-independent-reconciliation.json` | Editor and SDK delivery replay PASS; `dd89e84823a6` |
| `editor-controller-reconciliation.json` | Controller/task/separate editor cleanup PASS; `31c5fbdda69f` |
| `editor-export-reconciliation.json` | 310 regular files copied byte-exactly; three junction fixtures recorded |

The initial evidence-copy attempt could not read the Nathan-only generated
`brief.exe` fixture through the SSH account. A separate bounded Nathan export
succeeded without changing fixture ACLs. Its one-off task was removed and
inactive. Original junction targets are retained as metadata and represented by
relative directory symlinks in the host copy; guest NTFS ACLs are not reproduced
on Linux. All original guest evidence, earlier failures and diagnostic runs
remain intact. The profile cache has no asserted disk quota.

This completes the fresh development SDK, GTK and installed-editor campaign for
`5630388`. It does not replace the documented release qualification or remaining
feature restrictions in Status. Active SDK/VSIX locations, Lich and Anomalous are
unchanged. The latest accepted Linux SDK remains `d5e8758`; the newer Linux source
test results are distinct from installed Linux SDK acceptance.

#### Same-source Linux reference and Windows closeout, November 4

The development SDK comparison now uses `5630388` on both hosts. The main
checkout was temporarily detached at that already-pushed revision during
acceptance, then restored to `master`; no commits or working changes were
discarded. The intervening commits contained notebook changes only.

The first Linux refresh intentionally enabled its existing PostgreSQL server
fixture, exposing that the previous build lacked libpq development headers and
therefore had no PostgreSQL provider. That run failed the live database test and
skipped the client and display checks. The failure is preserved in
`563038813c7e-closeout/linux-candidate/`; it produced no SDK. Matching Ubuntu
`libpq-dev` 16.15 headers and Xvfb were downloaded and extracted into a private
dependency directory with recorded package hashes. No system package or service
was installed. The fresh v2 build records those inputs, uses the existing
bounded disposable database cluster and authenticated private X11 display, and
requires zero skipped CTests.

V2 passed all 568 CTests in 205.90 seconds, SDK installation/validation and all
1,184 installed consumer executions. Its validator also exercises the installed
shared-runtime ABI. The SDK contains eleven root tools, eighteen compiled STL
modules, eighty STL source files, 66 documentation pages and 259 sealed payload
files. The fresh archive was reopened and reconciled against every payload byte,
Unix mode and manifest record; the adapted archive verifier passed 39 controls
and independent review.

| Linux artifact | SHA-256 |
| --- | --- |
| `linux-candidate-v2/sdk/manifest.json` | `cb2eef4cda97ed6d508d16c27675bc75a4a141f3cb5d9387509969297180eb4d` |
| `linux-delivery-v2/nil-framework-linux-development-563038813c7e.tar.gz` | `6f2ed516ace2164919082a23e820af4ead19d4c3eaef9f18f7af0536d323e805` |
| `linux-delivery-v2/delivery.json` | `7611ee41747e9eebf291610f3879717fbdd839f4f216ed31533aef7ac7b37a58` |

Fresh installed checks passed on both platforms:

- Ordinary I/O: sixteen executions across both STL layouts and eight routes,
  with 36 command records, two invalid-ABI rejections, and independently replayed
  Unicode namespaces and raw file bytes.
- Interpreter checkpoints: capture, mutation, repeated restoration,
  continuation, rejection and explicit-exit cleanup against the installed
  interpreter. These raw NIL fixtures do not make an STL-layout claim.
- Debugger: 146 checks, twenty commands and fifteen sessions, with matching
  source-fixture and command-output identities across hosts.
- Unicode byte files: sixteen executions and 34 commands, independently checked
  against the frozen fixture and exact installed tools.
- Linux installed GTK: ten VM/native cases using the installed compiler,
  interpreter, tester and source STL on the private display. The harness retains
  its ten-case positive summary; it discards provider/lifecycle stdout, so this
  run does not claim an independently retained provider assertion count.

All runs preserved SDK/source input hashes and verified owned cleanup. The
Windows debugger copy additionally reconciles forty raw command logs, eleven
artifacts and fifteen session records. The prior Windows installed GTK and
88-check editor campaign remains bound to the exact same accepted Windows SDK.

The test-inventory comparison has 566 common registrations. Five Windows-only
checks cover its memory limiter, GTK loader, unmanifested UTF-8 path behavior,
UTF-8 console and SDK preflight. Two Linux-only checks cover POSIX process
semantics and ELF toolchain inputs. These match explicit CMake platform guards;
Windows has corresponding common process and MSVC toolchain contracts. Its
service-desktop skip remains a skip in the full-suite record, supplemented by
the separately accepted installed GTK gate.

`paired-sdk-comparison.json` also verifies matching identities for all 74
consumer fixtures, both STL layouts and eight routes, all eighty STL sources,
eighteen module/source/alias inventories, eleven tool names and 66 documentation
pages. This compares behavior and source inventories, not native binary bytes
or host ABI dependencies. Consumer evidence retains the previously documented
aggregate-only standalone-object and deleted temporary artifact limits.

All evidence below is under
`/home/nathan/vm/nil-win11/artifacts/563038813c7e-closeout/`:

| Evidence | Scope |
| --- | --- |
| `linux-candidate-v2/report.json` | Full Linux SDK pipeline, raw logs and input records |
| `paired-sdk-comparison.json` | Same-source SDK/test/consumer inventory comparison |
| `windows-shared-copy-reconciliation-v2.json` / `linux-shared-copy-reconciliation.json` | Installed ordinary-I/O and checkpoint evidence replay |
| `windows-tooling-reconciliation.json` / `linux-tooling-v1/report.json` | Installed debugger and Unicode gates |
| `windows-debugger-work-reconciliation.json` | Copied debugger raw artifacts and transcripts |
| `linux-installed-gtk-v1/report.json` | Fresh installed GTK10 and cleanup/input seals |
| `installed-platform-contract-comparison.json` | Normalized installed-contract comparison |
| `windows-platform-closeout-audit.txt` | Platform guard review and remaining scope limits |

Core development-profile closeout is supported by these results. Optional HAG
interop remains unqualified on Windows, and secure byte publication retains its
documented Win32 security and parent-path semantics rather than POSIX-specific
guarantees. The audit also found an experimental `nilagent` title-filter bug;
its focused follow-up is separate from these sealed SDK bytes. Shared code hot
reload, external-state rollback and native `environment.list` remain unsupported.
Windows release dependency/bare-host qualification and stable v1 publication
remain separate work. The active SDKs, Lich and Anomalous are unchanged.

## Focused nilagent window-filter follow-up

Windows window discovery now applies the same case-sensitive substring filter
as the Linux X11 implementation. Empty filters retain visible-window discovery;
hidden windows remain excluded. A shared predicate and the actual Win32
enumerator are covered by a focused regression fixture.

Verification after the accepted `5630388` SDK closeout:

- Linux: the actual `nilagent` tool built; both focused CTests passed, including
  eleven predicate checks, and `--version` passed.
- Windows: strict `/W4 /WX` fixture builds passed. Nathan's desktop run passed
  34 checks against three owned, nonactivating windows. Removing the production
  filter deliberately caused the expected assertion failure. Production
  `nilagent --version` also passed.
- The initial SSH service-station fixture failed because both requested-visible
  owned windows were actually invisible. An independent probe established that
  all three owned HWNDs were enumerable while the station lacked `WSF_VISIBLE`.
  The final test returns CTest skip code 77 only for that measured prerequisite
  condition, after destroying its windows. Desktop filtering assertions remain
  mandatory; the final service check confirmed the precise skip.
- One private build wrapper expected CRLF from `--version`; production correctly
  emitted LF under its binary-stream configuration. The original wrapper failure
  is retained, with raw-output reconciliation and corrected later expectations.

The final task was removed and inactive, owned child processes were cleaned up,
and the guest checkout plus accepted SDK/input hashes remained unchanged.
Evidence is under `/home/nathan/vm/nil-win11/artifacts/nilagent-window-filter-20261104/`;
`focused-reconciliation.json` SHA-256 is
`abf7b04e7d70cef520d514251b35739a3e1413944fe7d4a83cae55cb4f78ef1d`.
This is focused verification of the new source and private tool builds. The
sealed SDKs still identify `5630388`; their earlier test counts and bytes are
unchanged. A future full Windows service-suite run will register this additional
desktop-dependent test and report its prerequisite skip explicitly.

The subsequent macOS bring-up added numeric rounding/range controls and found
that rejected MSVC conversions could modify the output value. Commit `a7a05e5`
normalizes assignment-on-success. The exact private Windows probe then passed
all 190 checks under strict MSVC compilation, with SDK/checkout seals and cleanup
verified. Details and preserved failure evidence are in the
[macOS notebook](2026-11-04-macos-parity-plan.md#cross-platform-rejected-conversion-output).
This focused source verification does not relabel the accepted `5630388` SDK.

The assembler missing-output identity repair from macOS bring-up was also
checked on Windows at `0b2b884`: a private rebuild passed fresh x64 COFF output,
same-file rejection, hardlink rejection and absent-input output preservation
(4/4), reusing unchanged reference libraries. Checkout/SDK/input seals and owned
cleanup passed; the report is linked from the macOS notebook.


## 2026-11-04: subsequent compiler-identity source regression

The common `compilerFilesComplete` change at `e087964` passed a private Windows
Release build and four focused contracts (181 checks, zero skips), including
real VM/native incremental reuse. Existing SDK/VSIX/checkout and settings were
unchanged; the full paired SDK baseline remains `5630388`. Source/input/binary
seals and owned cleanup were verified. See the
[complete follow-up and retained setup failures](2026-11-04-macos-parity-plan.md#private-windows-compiler-identity-regression)
for exact evidence and the process-local DIA/Release try-compile configuration.


## 2026-11-08: complete local suite and the per-platform provider decision

The first complete Windows suite since the port closeout ran on the provisioned
VM at `8aa60d2`, against a same-commit Linux reference of 572 registered tests,
zero failures and three environment skips.

**Inventory.** Windows registers 574 tests to Linux's 572; the two share 569.
Windows adds console UTF-8, unmanifested UTF-8 file paths, the GTK loader, a
native memory-limit self-test and the SDK preflight. Linux keeps toolchain
inputs, POSIX process semantics and `nilinterpreter.ai-checkpoint-channel`. The
last of those is worth naming precisely because a Linux-only interpreter test
reads like a missing Windows feature and is not one: it drives
`socket.socketpair()` backpressure in the Python acceptance client, while the
product-level `nilinterpreter.ai-checkpoint-contract` is registered and passes on
both hosts. The Windows-only memory-limit entry is likewise additive, a self-test
of the launcher Windows needs because Linux obtains the same bound from
`setrlimit`. An earlier estimate of 571 Windows tests was wrong: it assumed
platform gating only subtracts from the reference inventory.

**No language gap.** Nothing in the language, frontend, type checker, IR,
interpreter, LLVM backend or standard library fails. Of the five reported
failures, four are an artifact of the harness rather than of the product. The
scheduled task had been registered `/ru SYSTEM`, and that account reports `TEMP`
as `C:\WINDOWS\TEMP`; `tempfile` propagates that casing into the paths handed to
the tools, the tools report paths in their on-disk casing `C:\Windows\Temp`, and
`matrix.annotations`, `matrix.execution`, `nil.bugpass-lexer-contract` and
`nilbuild.compiler-identity-contract` each assert an exact `path:line` substring.
All four pass unchanged when the same binaries are re-run under an ordinary user
account, where the environment variable, `tempfile` and the directory on disk
agree. Earlier green runs used a user account, so this was never product drift.

**A genuine platform difference, recorded rather than fixed.** The tools report a
canonicalized path on Windows, so a caller can receive a different string from
the one it passed; Linux echoes the argument verbatim. Normal accounts never
observe this, but exact-match consumers such as editor and LSP clients would.

**Two environment prerequisites the repository does not state.**
`nil.windows-sdk-preflight` invokes `powershell.exe -File scripts/build-sdk.ps1`
deliberately without a bypass, so all eleven subtests observe
"running scripts is disabled on this system" while the machine execution policy
is `Restricted` — the Windows client default. `nil.ui-display` and
`nilagent.window-discovery` self-skip for want of an interactive desktop session,
which SSH does not provide; Win32 UI acceptance will need a console or RDP
session on the VM rather than the headless path used for the language suite.

**Harness faults worth remembering.** A `call vcvars64.bat` redirected into the
same log the rest of a batch script appends to leaves a background Visual Studio
process holding that inherited handle; every following `>>` then fails with a
sharing violation, cmd reports it on the console rather than in the log, skips
the redirected command outright and sets errorlevel 1. Under `schtasks` the
console is discarded, so an entire configure/build/test script can appear to hang
at the vcvars banner while nothing after it has run. Give vcvars its own file.
Separately, the prebuilt LLVM 18.1.8 package hardcodes a VS2019 Professional DIA
SDK path from its build machine; `cmake/NilLlvmDia.cmake` detects this and
declines to guess a replacement, so the configure needs
`-DNIL_LLVM_DIA_LIBRARY` pointed at the installed `diaguids.lib`.

**Decision.** With core language parity established on Windows, desktop UI moves
to one provider per platform: GTK remains the Linux provider, Win32 becomes the
Windows provider and AppKit the macOS provider. GTK on macOS is retired; the
directed-input evidence at `8aa60d2` showed every accessibility query against a
GTK process returning `kAXErrorCannotComplete`, including the application
element's own role and attribute names, because GTK pumps GLib and never runs
`-[NSApplication run]` while AppKit services those requests from the main run
loop. Win32 is implemented first, since it can be exercised on a local VM.
