# Windows framework recovery and development SDK

## Status

Implemented; Windows 11 x64 and Linux validation in progress. This continues the
interrupted MSVC port at `3dc9d17`; it is not a Windows release or RC declaration.
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
The clean-revision Windows SDK pipeline remains the final acceptance gate.

Reproducible checks:

```text
cmake --build <build> --parallel 4
ctest --test-dir <build> --output-on-failure --parallel 4 --output-junit <report>
powershell -File scripts/build-sdk.ps1 -BuildDirectory <Windows Release build>
```

## Follow-up and boundaries

Complete the full Windows/Linux suites and the isolated SDK consumer gate, then
record their exact counts and checkpoint revision here. Windows remains a
development target: POSIX-only lifetime/project checks, optional display and
PostgreSQL providers, and atomic publication constraints must not be concealed
by a green executed-test count. The established RC/release promotion profile
remains Linux-only.

Windows staging preserves exclusive creation but inherits the parent ACL; it
does not promise POSIX-style owner-only isolation in shared output/TEMP trees.
The compiler/linker already had this limitation before the recovered port.
Explicit owner-DACL creation and cross-user ACL regression tests remain a
separate hardening task; use trusted user-controlled directories meanwhile.
