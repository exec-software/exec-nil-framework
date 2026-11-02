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
