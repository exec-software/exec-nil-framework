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
