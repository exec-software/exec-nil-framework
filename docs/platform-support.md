# Platform support and validation

Implementation, installed development acceptance and release qualification are
separate. This page records the current development evidence; the
[v1 release profile](release/v1-profile.md) defines the reviewed binary release
boundary. A source update does not update an installed SDK.

## Current development coverage

| Host | Verified development baseline | Remaining boundary |
| --- | --- | --- |
| Linux x86_64 | Clean `9fa354d` SDK: all 590 source CTests pass without skips; 1,184 installed consumer routes, twelve PostgreSQL routes, 146 debugger checks, execution checkpoints, twenty GTK desktop/input cases and all 88 editor groups pass. | Qualified in a private consumer environment. The reviewed Ubuntu release toolchain and shared deployment permissions require their own gates. |
| Windows x86_64 | Clean `9fa354d` SDK: 588 of 590 source CTests pass, with two headless display skips subsequently exercised successfully in Nathan's desktop. Relocated SDK passes all 1,184 consumer routes, twelve PostgreSQL routes, 146 debugger checks, execution checkpoints, twenty Win32 desktop/input cases, four desktop DAP sessions and all 88 editor groups. | The candidate needed a recorded, scoped read/execute grant on `nillib`. The permanent packaging correction separately passes real cross-account access tests; older SDKs do not acquire it automatically. Bare-host installation and release qualification remain open. |
| macOS 15 Intel x86_64 | Relocated `1efbe66` SDK passes all 1,184 consumer routes, twelve PostgreSQL routes, 146 debugger checks, execution checkpoints, twenty installed AppKit desktop/input cases, four desktop DAP sessions and all 88 editor groups. Current `59ffa97` source fixtures pass all 22 application/tooling/packaging CTests. | The installed editor needed a diagnostic rerun after a project-build timeout; it passed unchanged deadlines, with the original cause unconfirmed. The original full source report retains four failures resolved by later fixtures and one ELF-only execution skip. Shared deployment and release qualification remain open. |
| macOS 15 Apple Silicon arm64 | Relocated `1efbe66` SDK passes all 1,184 consumer routes, twelve PostgreSQL routes, 146 debugger checks, execution checkpoints, twenty installed AppKit desktop/input cases, four desktop DAP sessions and all 88 editor groups. Current `59ffa97` source fixtures also pass all 22 application/tooling/packaging CTests. | The original full source report retains four failures and one ELF-only execution skip; subsequent hosted checks resolve the four fixture failures. Shared deployment and release qualification remain open. |

Those pinned baseline SDKs contain eleven root commands, eighteen compiled
standard-library modules, their source, native runtimes and HTML documentation. The installed
consumer campaign covers both `nilstd` and `nillib`: 888 retained execution
results are independently replayed from raw reports, while 296 object/link
routes are checked by the pinned campaign driver. Each private candidate retains
267 payload files plus its manifest through relocation to a path containing
spaces and Unicode. Mac executables are checked as Mach-O for their expected
architecture. These candidates have not replaced accepted SDKs or activated a
release.

Current source SDKs additionally package `nil.std.markup`, bringing the logical
module inventory to nineteen. See the [current library status](standard-library/index.md);
the exact tools, source, binary modules and documentation are sealed in each
SDK's manifest.

The Linux editor result uses VS Code 1.144.1 in an isolated X11 session and empty
profile. Both current Mac application campaigns use VS Code 1.146.0. Each passes
88 distinct groups, all nine test modules, the full-suite completion sentinel
and no exclusions. Editor results using build-tree tools are distinguished from
results using an installed SDK.

Installed desktop/editor coverage is complete on both Mac architectures:
[ARM run 43146638765](https://github.com/exec-software/exec-nil-framework/actions/runs/43146638765)
and [Intel run 43149252918](https://github.com/exec-software/exec-nil-framework/actions/runs/43149252918).
The first Intel editor attempt timed out after 25 groups. With task lifecycle
diagnostics added, all 88 groups pass using unchanged SDK bytes and deadlines;
the Ny VM project group takes 161.171 seconds of its 180-second allowance.
All 31 observed task lifecycles complete. This establishes successful installed
execution, not a root-cause fix for the earlier timeout or a performance promise.

Exact revisions, evidence hashes, host preparation, interrupted attempts and
failure follow-ups are retained in the
[current parity notebook](../notebook/2026-11-11-web-runtime-parity.md).
Earlier baselines remain in the
[Windows/Linux closeout](../notebook/2026-11-02-windows-port.md) and
[macOS bring-up notebook](../notebook/2026-11-04-macos-parity-plan.md).

## Source failures and subsequent checks

The original full Mac LLVM-ON suites at `1efbe66` each record 583 passes, four
failures and one skip from 588 registrations. ARM failures are the test-only
AppKit input injection, two manual native-link fixtures missing framework
arguments, and a first project build exceeding its fixture deadline. Intel's
AppKit checks pass; its fourth failure is instead the first tool-discovery
project run exceeding a 60-second child deadline.

Subsequent test fixtures isolate AppKit injection from host compilation, pass
the runtime's required framework arguments, and give real Darwin project runs
bounded allowances that reflect toolchain fingerprinting work. Explicit short
process/error deadlines remain unchanged. The complete 21-contract campaign at
`eca43f1` passes on both architectures, including project build/run and tool
discovery, with four desktop DAP sessions and all 88 editor groups
([run 43141540789](https://github.com/exec-software/exec-nil-framework/actions/runs/43141540789)).
These results supplement the unchanged SDK; the original failed reports remain
failed. The ELF-only freestanding executable fixture remains an explicit
format-specific skip. Actual Mach-O archives separately pass import validation.

The next campaign at `59ffa97` adds mixed-source/native SDK packaging, including
the destination-access regression: all 22 selected CTests, four desktop DAP
sessions and all 88 editor groups pass per Mac architecture
([run 43148956133](https://github.com/exec-software/exec-nil-framework/actions/runs/43148956133)).
The exact pinned thirteen-test packaging invocation passes; its original JUnit
output retains only eight complete result lines because CTest truncates passing
output at 1,024 bytes. Future runs retain a bounded 64 KiB per SDK/project test.

Windows's former transient native output-replacement failure is corrected at
`9fa354d`: compiler and linker share bounded Windows sharing/lock retries and
preserve existing output on persistent failure. Strict MSVC controls exercise
real held handles, read-only output, Unicode paths and exact byte preservation;
the former single-attempt implementation fails the transient-release control.
The complete source suite then has only the two headless display skips above.

LLVM-OFF builds exercise frontend/interpreter behavior and rejection of disabled
native operations. Their passing tests do not establish native execution.

## Native code and tools

Both Mac architectures pass native incremental reuse and invalidation. Compiler
identity hashes actual loaded files and exact shared-cache backing files;
native linker probes collect 8,321 inputs. Unmodeled dependency profiles rebuild
rather than reuse stale output. SQL runtime loader paths are scoped to execution
processes so they do not change ordinary compiler/linker identity.

Bounded x86_64 and aarch64 native inline assembly are described in the
[assembly contract](advanced/inline-assembly.md). Thin Mach-O64 x86_64 and arm64
objects/executables support bounded context inspection, opt-in annotation
embedding and dead-strip retention. See the
[container contract](advanced/executable-context.md) for supported forms.

ELF, COFF and Mach-O freestanding runtime archives are checked for malformed or
mixed object profiles and unexpected imports. Actual MSVC no-CRT controls link
and execute with only the explicitly selected host exit import. This does not
claim a cross-platform bare-metal application profile.

Managed and owned native lifetime tests pass on both Macs. The owned workloads
allocate and free 36,000 guarded buffers per memory mode; withholding frees
reaches the 96 MiB retained-payload bound. Darwin measures intercepted
malloc-family payloads, excluding allocator metadata and mappings. Linux's
address-space limit and Windows Job limits retain their platform-specific
metrics; these are not identical whole-process memory measurements.

## Desktop and runtime control

The UI provider is GTK4 on Linux, Win32 on Windows and AppKit on macOS. Linux
needs an external GTK4 runtime. Display/input tests need an interactive window
server; headless skips do not count as passes. Source AppKit campaigns pass
provider, lifecycle, layout, focus, sensitivity, Unicode entry and real
interpreter/native desktop/input contracts on both architectures. Test input
uses each owning application's responders, without claiming general desktop
automation or cross-process accessibility support.

`nilinterpreter --ai` provides opt-in local control through Windows named pipes
or Unix sockets. Pause, resume, instruction stepping and bounded inspection use
the real VM execution thread. `--ai-wait` starts paused before `main`.

`vm.snapshot` and `vm.restore` provide reusable **same-process** execution
checkpoints for paused deterministic VM programs: execution position, frames,
values, heap identities and admitted runtime state. Open resources, external
effects, native/JIT execution, cross-process persistence and code hot reload
remain outside this profile. See [NIL-0006](design/NIL-0006-ai-module.md).
DAP keeps VM execution, inspection and UI cleanup on the process main thread.
Passing interpreter debugging does not establish native debugging.

## Web-runtime APIs and deployment boundaries

Sockets, secure randomness and text-byte APIs pass the same six interpreter
contracts on Linux, Windows and both Macs without skips. These additions require
the interpreter on every host because the native text ABI does not yet preserve
arbitrary embedded NUL bytes. File timestamps use native filesystem metadata
across interpreter and native routes; text/binary NIL, JIT and AOT checks pass
on all four hosts. PostgreSQL acceptance uses an isolated owned server and an
external client runtime; it does not bundle or qualify every provider version.

The SDK builder honors `CMAKE_BUILD_PARALLEL_LEVEL`. SDK publication checks
cover staged layout, relocation, retained payload bytes and modes, and bounded
replacement. Cross-account Windows use additionally exposed a binary-library
access defect: the private temporary compilation directory's DACL survived its
move into the SDK. Packaging now copies validated, completed outputs into fresh
destination entries, inheriting normal SDK access without exposing private
compilation or changing machine ACLs. Linux tests cover restrictive umasks,
unchanged bytes and failed-copy rollback; a separate limited Windows account
reads all 102 published library files while remaining unable to read private
staging. These checks do not retroactively change older SDK permissions or
qualify arbitrary shared installations.

The [release gates](release/v1-profile.md#qualification-and-publication) still
apply before publishing new binaries. Current development coverage does not
expand the reviewed Ubuntu release profile, establish a bare-host Windows or
macOS installation, or imply a stable v1 release.
