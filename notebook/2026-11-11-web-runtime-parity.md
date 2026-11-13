# Web runtime and platform parity — 2026-11-11

The web handoff began at framework `dfb7ad7` on `nil-sockets-entropy-bytes`
([PR 38](https://github.com/exec-software/exec-nil-framework/pull/38)) and web
`594579a` plus preserved local changes. The archived originals are under
`atoma/archived-exec-ny-web` and `atoma/archived-exec-ny-webview`. Platform work
comes first; the web changes are not discarded, reset or overwritten.

## Starting baseline and boundaries

Windows now uses Win32 and macOS uses AppKit; Linux uses GTK4. November 9 native
AppKit provider and interpreter display results supersede the earlier private
GTK experiments. The AppKit display workflow accidentally selected `nilbuild.*`
with the regex `nil.ui`; its selector is now anchored to the `nil.ui` namespace.
The original failing cache contract remains registered in the full suite.
The full Mac source workflow receives the measured 35-minute aggregate CTest
allowance already used by the successful Intel SDK retry. Individual test
deadlines are unchanged.

The new socket, entropy, byte-text and timestamp APIs were initially
interpreter-only. Earlier passing installed SDK campaigns do not validate these
new APIs. Darwin incremental cache identity, its native memory-bound tests and
current native application/editor acceptance remain open. The platform/readme
documentation now separates current source implementation from the recorded SDK
and route baselines.

At recovery, source validation push run
[43095105713](https://github.com/exec-software/exec-nil-framework/actions/runs/43095105713)
failed before any runner executed: GitHub reports an internal server error,
correlation ID `0fbc4710-ecca-4198-a05c-f35c1d8d4bef`. It supplies no product
test result. The corresponding PR run was still queued during recovery.

## Portability findings being corrected

- Windows `SOCKET` values were narrowed into `int`, and its `SO_REUSEADDR`
  semantics did not enforce the intended exclusive listener contract.
- Darwin writes lacked per-socket SIGPIPE suppression.
- Finite readiness waits could restart their complete timeout after interruption.
- Windows `rand_s` declarations depended on defining a macro before earlier
  headers had already included the CRT.
- Socket ownership was not released at every VM terminal/destructor path.
  A process-exit test only observes operating-system cleanup; a new same-process
  lifecycle test must observe the VM releasing the listener itself.
- A post-bind reuse-port setter cannot enable a second listener before that
  second bind occurs. Sharing requires an explicit pre-bind constructor and
  accurate platform support, not silently weakening ordinary listener isolation.
- File timestamps were converted using two separate clock readings. Repeated
  reads of an unchanged file could differ by a millisecond. Native metadata
  provides a stable epoch value; the scalar API can share that implementation
  between the interpreter and native runtime.

The focused web-runtime workflow builds actual host code and runs an exact six-test
inventory on Linux x86_64, Windows x86_64, Intel macOS and Apple Silicon macOS.
LLVM is disabled there; the report states that scope. The local Linux build and
private Windows VM build additionally exercise available native timestamp routes.
No installed SDK is replaced by these checks.

The timestamp change now passes eight exact Linux observations: textual NIL,
binary NIL, LLVM JIT and AOT, each before and after a metadata change. Each
observation repeats the unchanged timestamp 100 times; two additional VM cases
reject NUL paths. Fifteen owned commands and thirty raw streams were independently
replayed. The report SHA is
`135a70a9ed8b8a14cda4122abf0b5b3bdbc08378ca376888907c6e5ba9ceff1f`.
The old SDK fails the pre-epoch fractional timestamp control (`-1` versus the
correct floor of `-2` milliseconds). Native Windows adapter checks also pass
116 assertions under strict MSVC warnings; subsequent NUL-host controls and the
full integrated VM build still require their own verification.

The corrected Linux integration runs now pass nine selected contracts: adapter,
same-process socket lifecycle, ten socket language cases, timestamp helper and
route checks, 38 Matrix execution fixtures, execution checkpoints, VM faults and
parallel runtime. The initial fixture failures are retained alongside the final
results under `parity-web-20261111/linux-integrated-v1` in the VM artifact tree.
These are results across the corrected runs, not a claim that the first run was
green. The macOS display harness now requires all three known desktop cases on
every requested route and prints the full language report into retained CTest
output. Positive and five malformed-inventory controls pass locally; actual
LLVM-enabled AppKit execution still needs both hosted architectures.

The first hosted campaign at `45f5569` retained actual failures: Linux passed all
six contracts; both Macs failed to compile because Darwin declares `getentropy`
in `<sys/random.h>`. Windows LLVM-OFF passed socket adapters, same-process cleanup,
timestamp adapters and all ten loopback cases. Its timestamp fixture used the
reserved DOS device name `nul.ny`, and its source-map test compared an unresolved
temporary path with the compiler's canonical path. The fixture now uses a normal
filename and resolves the source path before compilation. The separate Windows
LLVM-ON build exposed the RPC header's `small` macro colliding with the new
backend code-model enum. Capitalized enum members preserve the CLI spellings;
a real Windows-header-first compilation protects that interface. Its MSVC check
passes. The Linux rebuild and Matrix, timestamp and freestanding contracts pass.
These corrections require fresh hosted execution; the original failures remain
under `parity-web-20261111/github-web-43113972394` and the private Windows evidence.

At `965f159`, the fresh Windows LLVM-ON source build and all six focused contracts
pass with no skips: 118 adapter assertions, 184 VM lifecycle assertions, 129
timestamp helper assertions, 38 Matrix fixtures, ten loopback cases and the public
timestamp route contract. The full run took 103.79 seconds and preserved the old
dirty guest checkout and accepted SDK. Its report SHA is
`d1d823e482290dd3044d872a97d1ca79ecadfb22a1e858a1a4972bf8f3fe1649`.
A separate eight-case Windows probe confirms that textual and binary source maps
embed canonical long paths even when the compiler receives an 8.3 TEMP alias.

The completed hosted web-runtime run `43116117693` passes all six contracts on
Linux, Windows, Intel macOS and ARM macOS. Independent replay checked 148 artifact
hashes and each host's timestamp byte streams. Its checked-out PR merge is
`cec4364d`, with requested head `19bd5f1` retained as a separate identity. AppKit
run `43115312796` passes all five CTests and all six interpreter/native desktop
cases on both architectures at exact head `965f159`. The LLVM-enabled Linux
source workflow at `19bd5f1` also passes all 579 CTests with no skips; its LLVM-OFF
and extension jobs pass.

## Darwin compiler cache identity

The compiler now has a bounded Darwin loaded-image collector. It matches actual
headers (including dyld) to exact cache mappings and UUID tables, preserves
ordinary loaded paths and returns the complete backing-file set for ordinary
full-content SHA256 hashing. UUIDs are attribution metadata, never cache keys.
Unknown APIs/profiles or unstable observations remain uncached; known readable
inputs are retained for output protection even when the inventory is incomplete.
CommonCrypto computes the same digest as the portable SHA implementation.

The builder hashes shared toolchain files once per preparation phase and once
again in a fresh verification phase. It never carries those values across builds
or compilation boundaries. Consumed sources and manifests, including symlink and
hard-link aliases, are excluded from memoization. Seventy portable controls,
36 explicit identity checks and retained real Intel/ARM metadata replay pass.
The coordinated Linux builder, fingerprint and source-protection regressions
also pass. Actual Apple API execution is a required hosted test. Native linker
input closure is being implemented separately; compiler identity alone does not
establish native incremental reuse.

The first native closure implementation hashes the selected Apple driver,
linker and helper libraries, compiler runtime, and the SDK's linkable namespace.
It resolves loader search paths per executable and preserves selected aliases.
Unknown linker options, external fallback files and injected loader/search
environments disable reuse. Fifty-nine portable checks and all 407 complete
TBD files in the ARM census pass; the other 808 captured prefixes are truncated
and require actual hosted full-file reads. The observed `-L/usr/local/lib`
driver option is still outside the admitted profile, so this implementation
does not yet establish default native cache parity. The application workflow
now requires the actual complete VM/native incremental contract on both hosts.

The next closure slice models the observed explicit `/usr/local/lib` search
option using Apple's SDK rerooting rules and a fresh candidate lookup on every
collection. Supported stub shadows and their transitive re-exports are hashed;
unmodeled binary shadows and archive autolinks disable reuse. Seventy-seven
Linux checks and sanitizer controls pass. The portable fixture's path keys now
use generic separators; actual MSVC compilation and all 71 Windows controls
pass (the additional six Linux controls require symlink creation). The source
campaign's original Windows fixture failure is retained. Hosted default native
reuse remains required; `f79b94b` proves VM reuse on ARM but correctly refuses
the still-unmodeled native search profile in that revision.

## Darwin allocation retention and editor coverage

The two native lifetime programs remain unchanged. Darwin now has a test-only
interposer to enforce **96 MiB of live requested native heap allocations**.
This metric excludes allocator overhead, the fixed tracker, mapped images and
allocation paths outside the intercepted API; it is not a claim of whole-process
address-space/commit parity. The harness audits the generated binary's allocator
imports, requires an injection handshake, checks the unchanged loops' allocation
counts and repeats each exact executable while withholding real target frees.
Retained untouched allocations must also fail, so a sampled footprint cannot
substitute for the allocation-retention check. Linux RLIMIT_AS and Windows Job
tests retain their original behavior.

GCC and Clang portable accounting controls pass: over 2 GiB allocated and freed
with a 64 KiB peak, plus realloc growth/shrink/failure, alignment and overflow
controls. Untouched retention and withheld-free controls reject at 96 MiB.
These local controls do not establish Mach-O injection. The new application
workflow requires real Intel and ARM execution, the existing native timestamp
routes, and the full VS Code extension-host campaign with no recorded exclusions.
It uses a fresh profile and Microsoft's architecture-specific VS Code 1.146.0
archives, pinned by SHA256 from the official update service. All results are
development/build-tree checks; no installed SDK or user editor is activated.

The first application campaign at `19bd5f1` exposed two setup errors. The public
Apple SDK does not ship dyld's interposition convenience header; the fixture now
declares its own pointer pairs in the documented Mach-O interposing section.
The pinned editor archive uses a different executable name; the launcher now
reads `CFBundleExecutable` from its own verified app bundle. Both failures are
retained, and actual injection/editor acceptance remains pending a corrected run.

At `2632bc6`, application campaign `43118620968` (PR merge `4eac1494`)
passes the real compiler loaded-image collector and all native timestamp routes
on both architectures. Both allocation controls exit before their report
handshake; this is retained as a failed test, not a memory-retention pass. The
editor now launches but its deep profile exceeds macOS's Unix socket path limit.
Its owned fixture now uses a short temporary root while retaining Unicode paths;
launch records identify the exact retained profile logs. Both artifact manifests
were replayed locally: 104 matching files each and unchanged source.

The complete Windows `2632bc6` build exposed an unconditional GNU weak-function
attribute in the freestanding archive. MSVC now uses a COFF alternate symbol for
the default failure hook. Separate executable link controls require the real
archive with and without an application-provided hook; the Linux archive and
existing freestanding execution contract pass. Actual MSVC archive creation,
default/override linking and both executions now pass all six phases in the
private VM, with owned cleanup and unchanged source. The full Windows campaign
will use the next committed source; its earlier failure remains recorded. The
current `2632bc6` LLVM-enabled Linux campaign also passes all 580 CTests, no skips.

The `f79b94b` early Darwin controls identify the initialization failure as a
duplicate-pointer accounting event: libmalloc can nest typed and untyped public
allocator calls. The fixture now accounts only for the outer request using a
fixed per-thread table, avoiding first-use TLS allocation. An explicit nesting
shim reproduces the old failure; ordinary/nested positive, untouched-retention
and withheld-free controls pass under both GCC and Clang. They include startup,
realloc preservation and overlapping threads. Coverage counts now subtract the
ready snapshot. Actual Darwin qualification remains pending this correction.

The shortened Mac editor profile launches successfully. At `f79b94b` the ARM
Problems check fails because the test requests `/tmp` diagnostics while the
compiler correctly reports the physical `/private/tmp` file. Retained terminal
output and Problems entries show the expected compiler source, line and message.
The harness now checks physical file identity with its existing alias helper,
while still requiring those diagnostic fields. This is not an editor pass until
the complete real-host suite finishes; no checks are excluded.

The native symbol-surface harness now reads COFF and Mach-O through explicit
LLVM tooling instead of skipping those hosts. It preserves the same runtime
import allowlists and checks freestanding entry/host interop and IR attributes;
ELF x86_64 relocation assertions remain identified as that format's extra
coverage. Six actual Clang-emitted format/architecture fixtures validate the
reader. All sixteen generic checks pass with the real Windows `f79b94b` compiler
and assembler; Linux passes those plus both ELF relocation checks. Mach-O
framework execution remains in the next hosted campaign.

At `a518138`, both actual Mac allocator control sets pass. ARM also passes the
unchanged managed-lifetime workload and all sixteen generic Mach-O symbol
checks. Owned-lifetime coverage exposes a fixture assumption: `owned<i32>(16384)`
allocates 65,540 bytes, including its four-byte guard, rather than 65,536.
The target size is corrected with the Ny source unchanged. Twelve portable
control cases pass under GCC and Clang. The exact retained Ny workload, linked
against the real Linux runtime with accounting wrappers, observes 36,000
allocations and frees in each memory mode; withholding those exact frees rejects
at 1,535 retained blocks. The 96 MiB bound is unchanged. Hosted confirmation
of both owned memory modes remains required.

The Mac synthetic toolchain fixture now canonicalizes its private temporary
root, avoiding the incidental `/var` alias while retaining deliberate symlink
controls. Native dependency refusal reports the first bounded, sanitized reason;
environment values are never included. Eighty portable controls pass. An early
actual-host probe builds only the runtime and dependency collector, so further
SDK/linker failures are visible before the LLVM application build finishes.
It validates input discovery only; full-content identity and real incremental
reuse remain separate mandatory tests.

The Windows `f79b94b` full run finishes with 583 registrations: 579 pass, the
known portable path-fixture failure, and three skips. Follow-up evidence fixes
the path fixture, executes the sixteen COFF checks and passes both desktop gates
in Nathan's session (ten language VM/native UI/keyboard cases plus window
discovery). All 1,138 retained route successes and 78 expected native rejections
replay correctly; PostgreSQL's six routes and owned cluster shutdown pass too.
Source, installed SDK and the original dirty guest checkout remain unchanged.
The real Windows editor campaign now passes all 88 check groups without exclusions,
using the `a518138` physical-file alias helper against the unchanged `f79b94b`
products. Its first launch missed a required test-only Node setting and is
retained as a setup failure. The second run's private outer wrapper incorrectly
required 100 groups; the authoritative full-host report and all six process phases
pass. Independent replay verifies 34 retained file hashes, twelve raw streams,
matching run identity, cleanup and unchanged source/tools. The raw outer failure
remains preserved. Extension unit tests pass 161 cases with the one explicitly
inapplicable POSIX FIFO case skipped. Future host reports record full-suite
completion and the exact completed module list instead of an arbitrary group count.

## DAP execution ownership

DAP previously constructed a new worker for every continue or source step.
AppKit cannot create or poll its windows there, and desktop resources could
cross owning threads after a stop. VM construction, loading, execution,
inspection and disposal now stay on process main; one protocol reader forwards
bounded requests and accepts cooperative pause/terminate while instructions run.

Fourteen lifecycle sessions verify normal, EOF, malformed-frame and disconnect
paths, including clients that keep stdin open. A strict wire oracle requires
acknowledgement before continued/debuggee events. Four real GTK desktop sessions
exercise retained handles and queued events across stops, and natural disconnect
with an open window. All existing debugger tests pass. GTK alone cannot prove
the fix: the old adapter can reuse pthread IDs and pass that test. A deterministic
stream observer instead checks the actual writer of two debuggee output events;
it passes the new adapter and rejects the exact previous implementation.

At `8376252`, Windows passes all six exact debugger CTests, fourteen lifecycle
sessions and four real Nathan desktop sessions. Independent replay parses 318
raw DAP frames and verifies 42 acknowledgement/continued orderings. Both actual
Mac hosts also pass those six CTests and four AppKit desktop sessions in run
`43124577210` (tested PR merge `b3661e8`). Linux's complete LLVM-ON source suite
passes all 586 registrations without skips in run `43124577017`.

The same Mac campaign passes all eight selected native contracts on each host:
managed lifetime, both owned memory modes, generic Mach-O ABI checks, timestamps,
compiler identity and default/overridden host hooks. Independent replay verifies
306 retained file hashes per host and the raw allocation events. Each owned mode
allocates/frees all 36,000 guarded buffers; withholding frees reaches the 96 MiB
retained-payload bound. This does not claim a whole-process virtual-memory bound.
The shared six web-runtime contracts also pass on all four hosts at this revision;
148 retained file hashes and exact inventories replay successfully.

Both Mac editor sessions reach 25 successful groups before the aggregate
45-second project check/build/run deadline expires. The project check succeeds
and the VM build reaches post-compilation verification; the current real cache
contract takes roughly 110 seconds. The editor harness now records each command's
duration and allows 180 seconds for this three-command Darwin group, retaining
the full-host bound. Full editor acceptance remains open. Early native probes
identify the remaining cache refusals: an arm64e system launcher inspected as
arm64, and a non-linkable EmailCore plugin embedded in the SDK.

The native collector now inspects every tool slice, including Apple's arm64e
launcher, and conservatively follows every existing candidate from merged
runpaths. SDK omission requires a structurally valid executable/bundle in every
slice; object/dylib/stub, mixed, unknown and malformed images still disable reuse.
The ordinary link plan does not admit `-bundle_loader`. `OTOOL_PATH` is rejected
as an unmodeled tool selector. All 131 portable controls pass strict warnings
and ASan/UBSan, including transitive alternate-runpath dependencies and malformed
universal headers. This implementation still needs the actual hosted probe and
positive native cache reuse to pass.

Paused windows are not pumped, and adapter transport failures use best-effort
terminal notification before the reader accepts disconnect/EOF; cancellable
arbitrary input streams are outside this slice. No new native debugger or
checkpoint profile is claimed.

Native socket/byte/entropy parity needs a binary-safe value boundary. The new
VM APIs deliberately carry embedded NUL and arbitrary bytes in `text`; the
current native text ABI and several LLVM operations use null-terminated strings
and `strlen`. Merely registering these intrinsics with that ABI would silently
truncate legitimate network data and entropy. The scalar timestamp operation can
use the existing ordinary-I/O ABI; the byte APIs require an explicit length-aware
design and cross-route tests before native support is claimed.

This native limitation also exists on Linux. Host parity for these APIs means
equivalent interpreter behavior and consistent native rejection. A subsequent
counted-text ABI should retain one machine-word language values by pointing to
an immutable length/data descriptor, migrate language literals and runtime text
consumers together, adapt C-string FFI boundaries explicitly, and reject mixed
old/new object and runtime ABIs. A hidden pointer-length registry would retain
the same migration burden while adding lifetime and registration ambiguity.

At `860750e`, both actual Mac native-input probes collect 8,321 regular inputs.
Both native incremental steps also pass: ARM's verified report contains 131
closure and 78 real/fake incremental checks, including positive VM/native reuse
and invalidation of changed bytes with unchanged timestamps. Intel's successful
step takes roughly 387 seconds. Its final 306-file artifact manifest has also
been independently verified, with all six debugger, eight native and two
incremental CTests passing without skips and source unchanged.

A subsequent source review closes an additional cache-admission gap: an SDK
archive selected for the direct c++/System library roots could add unmodeled
link options. The collector now follows the supported linker's directory order
and requires the selected roots to be captured text interfaces. It permits
unrelated or shadowed archives while rejecting a newly exposed archive fallback.
All 143 strict-warning and ASan/UBSan checks pass; the old source fails the new
stub-removal regression. Default hosted acceptance of this refinement remains
required.

The ARM editor at `860750e` completes 32 groups, including all four Ny/Matrix
VM/native project check/build/run groups. Measured totals are approximately
48–50 seconds for VM and 90–93 seconds for native. Its next failure is a lookup
assumption for a closed imported file: the correct single diagnostic is present
under `/private/tmp`, while the test reads `/tmp`. The test now collects findings
by physical file identity without deduplicating them, preserving the exact
one-finding, source, location, dirty-buffer and external-edit checks. Full editor
acceptance remains pending.

The Intel editor at the same revision completes 26 groups. Its VM project
check/build/run takes approximately 104 seconds; the subsequent native build
alone takes 144 seconds, leaving too little of the three-minute aggregate
budget for the separate run task. The native project group now has a five-minute
Darwin budget; VM retains three minutes, other checks retain their bounds and
the full host still has a fifteen-minute deadline. Phase timings and exact
successful exit assertions remain required. The original failure is retained.

## AppKit interaction behavior follow-up

A comparison with the actual native controls found gaps beyond the old
cross-process input fixture restriction. AppKit's container sensitivity affected
only model flags, focus queries remembered the last explicit request, native
window resizes did not rerun layout, and editing could exceed the shared entry
scalar limit. The provider now propagates effective sensitivity while preserving
local flags, maps the real responder/field editor back to its widget, relayouts
on resize and validates edits through an NSFormatter. NSString conversion retains
embedded NUL bytes so the common text validator rejects them.

New real-control assertions cover disabled descendants and later children,
nested re-enable behavior, externally changed focus, field-editor termination,
native resize, astral Unicode at the 16,384-scalar limit, bounded replacement and
NUL rejection. At `40017f1`, hosted run `43127447798` passes the real provider on
both Macs: 156 display checks and 41 in-process keyboard checks per host, plus
availability. This targeted run does not yet qualify the new Matrix input shim.

Further comparison confirms actual shared layout gaps in Win32 and AppKit:
hidden children retain slots, a lone spanning control can be clipped to a
fraction of its requested size, and grids ignore expansion. Nested containers
also fail to inherit descendant expansion. These affect real layouts beyond
font/pixel differences and are being corrected with shared bounded allocation
math and native geometry assertions against the same public Context API.

The shared allocator now preserves full spanning requisitions, removes hidden
children and gaps, propagates expansion with explicit overrides and fills the
cross axis. Native coordinate conversion saturates extreme nested grids, and
per-layout child/expansion/measurement caches with iterative placement avoid
recursive depth and repeated-subtree work. Strict GCC, ASan/UBSan and actual
MSVC each pass 21,443 portable checks. The identical public geometry fixture
passes in the real GTK reference (4,603 provider checks) and Nathan's Win32
desktop (217 provider checks). The Windows report is retained at
`ui-layout-windows-v1/desktop-69b47920e5/evidence/report.json`, SHA-256
`db3b5a9e9d9737460ea75d50828e265eb5bf557eeb490331ec43cbb60ba6c5ed`.
These are pinned working-tree builds; AppKit and final-SHA SDK checks remain.

The new test-only AppKit input library drives only its own verified fixture
window through AppKit responders. The unchanged Matrix interaction suite must
complete both cases on each interpreter/native route and emit matching token,
parent/child PID and phase evidence. Seven portable harness controls pass,
including failed/timeout stream retention and malformed-evidence rejection.
The library is excluded from SDK installation. Actual hosted compilation and
input execution are the next gate; no Accessibility or global-input claim is made.

At `1efbe66`, Linux's complete LLVM-ON source suite passes 589/589 CTests
without skips in 377.54 seconds (run `43129952409`); LLVM-OFF and extension
jobs also pass. The four-host web-runtime campaign (`43129952445`) again
passes the exact six selected contracts per host, with all 148 retained files
independently hash-verified. Local Linux's full desktop contract also passes
all ten VM/native language cases after supplying the existing private Xvfb
prerequisite and building the lifecycle fixture; earlier prerequisite failures
and skips are retained separately.

Hosted AppKit run `43129956297` passes all seven selected CTests on Intel,
including all ten desktop/interaction language cases. ARM passes six of seven:
layout, provider and all desktop cases pass, and both VM interaction cases pass.
Native interaction compilation fails because the test-only arm64 input library
is inherited by Apple's arm64e compiler launcher. This is an input-harness
scope error; a follow-up must keep application injection out of compiler
children before ARM's language interaction gate can pass. Retained route
reports and per-process input records have been independently replayed.

The correction uses a same-architecture test compiler adapter: it validates
the explicit compiler/input declaration, removes only fixture-owned injection
and variables, then replaces itself with the real compiler. Ordinary arguments,
environment, PID and exit status are preserved; niltest and application children
retain the input driver. Ten controls pass, including actual subprocess checks
and rejection of mismatched injection or compiler targets. The adapter has no
SDK install rule. At `5a98403`, hosted run `43132072573` passes all seven UI
CTests on both architectures, with no case skips. Both retain all six desktop
and four interaction language cases. Root independently replayed each route's
niltest report against the token/PID input files and unchanged Matrix source.

The first fresh Windows SDK build at `1efbe66` exposes a separate fixture
portability error: Win32's COM headers define `interface`, which collides with
a local variable in the Darwin closure test. Commit `60d4c99` changes only that
identifier. Actual MSVC then passes 137 closure checks; Linux passes 143
(six additional POSIX symlink controls). The failed build remains recorded,
and the clean Windows SDK retry uses that exact new commit. No runtime code
or accepted installed SDK changed in this correction.

Both `1efbe66` Mac editor runs complete 37 groups, including all project and
real imported-file checks, before a fake compiler emits an alias path rather
than its captured canonical source identity. A real Linux editor under aliased
TMPDIR reproduces that failure. The fixture now returns the canonical captured
snapshot without changing its text. The full alias run then exposes the same
assumption in semantic navigation expectations for clean, uncaptured disk
sources. Those expectations now accept physical identity only for clean saved
targets without tabs; dirty, untitled and open-tab targets still require exact
URIs. Reference cardinality and duplicate checks remain strict, and new checks
verify that even clean open imports retain their editor URI.

The corrected real Linux host passes all 88 groups, all nine modules and no
exclusions in 44.7 seconds using VS Code 1.144.1. Analyzer bytes are unchanged.
The complete failing/corrected chain is retained under
`editor-workspace-alias-linux-q8g3r09h`; hosted Mac confirmation remains open.

## SDK binary and full-suite follow-up

The clean Windows `60d4c99` candidate records 586 passes and only the two
headless desktop skips from 588 registrations, and stages eleven root tools,
eighteen checked library modules and compiled documentation. A separate binary
audit nevertheless finds MSVC-generated `memcpy` and `memset` imports in its
freestanding archive. The old validator inspected only ELF objects and silently
accepted other formats. That candidate therefore remains unsuitable for SDK
parity despite its otherwise passing recorded gates.

MSVC-specific volatile byte destinations prevent those two loop substitutions
without changing GCC/Clang optimization. A new bounded archive reader checks
ELF, COFF and Mach-O symbol tables and rejects malformed or mixed profiles;
unknown formats no longer look import-free. Actual MSVC A/B archives prove the
old imports disappear, while default/override hooks and the expanded heap/copy
checks still pass. Both corrected no-CRT PE hosts link with `/NODEFAULTLIB`,
import only the explicitly selected host `ExitProcess`, execute and exit zero.
Fifteen parser groups, twelve actual Clang cross-format positive/negative
archives, Linux's built runtime and the hosted ARM SDK archive also pass.
The new CTest and SDK validator both enforce the archive boundary. Final Windows
evidence is `freestanding-current-v3/report.json`, SHA-256
`3bbf0218c33e105d69fa9a0df31bc38fadfd32043fd2bb4983c63dd215c1677a`.
A corrected clean Windows SDK must replace the candidate qualification.

The ARM `1efbe66` SDK preserves all 268 payload files through relocation and
passes all 1,184 installed consumer routes, 146 debugger checks/15 sessions,
execution checkpoints and twelve PostgreSQL source/binary routes. All 920
retained evidence files were independently hash-verified. Its source suite is
583 passes, four failures and one ELF-only freestanding execution skip; the
installed results do not erase those failures. In addition to the now-corrected
AppKit input fixture, two manual native-link fixtures omit Cocoa/CoreGraphics,
and the first fingerprinted project build exceeds its 45-second deadline.

The raw host links now carry the runtime's existing framework dependencies.
Only real Darwin project build/run calls receive the measured VM/native budgets
(180/300 seconds), retaining explicit shorter process/error-test deadlines and
phase timings; their complete CTest is bounded to 900 seconds. The application
workflow now admits the exact project, assembler, embedded-context and archive
contracts as well as its existing native/editor checks. Seven relevant local
CTests pass in 12.99 seconds; actual hosted follow-up remains required. Full SDK
workflows also upload their source inventory immediately, before lengthy
installed campaigns, so failed gates are visible without waiting for packaging.

## Source checkpoint at `9fb6c04`

At `9fb6c04`, Linux's complete LLVM-ON suite passes all 590 registrations
without skips in 258.36 seconds; LLVM-OFF and extension jobs also pass
([source run 43135334607](https://github.com/exec-software/exec-nil-framework/actions/runs/43135334607)).
The raw log independently yields 590 unique passing test names, including the
new freestanding archive-import contract. The tested PR merge is
`94979957a693bf725211001cc1434c14c1fbef3c`, distinct from the feature head.

The matching [web-runtime run 43135334608](https://github.com/exec-software/exec-nil-framework/actions/runs/43135334608)
passes the exact six interpreter contracts on Linux, Windows and both Macs,
with no failures or skips. All 148 retained evidence files were independently
hash-verified, together with unchanged source identity, exact JUnit inventories
and the retained timestamp command/cleanup reports. These focused checks do
not qualify native socket support or replace the installed SDK campaigns.

Both architecture jobs in
[application run 43135334578](https://github.com/exec-software/exec-nil-framework/actions/runs/43135334578)
pass all twenty exact CTests, four owned desktop DAP sessions each and the complete
VS Code 1.146.0 host: 88 unique passing groups, all nine modules, no exclusions
and the full-suite completion sentinel on each host. All 620 evidence files
match their manifest hashes and the source remains unchanged. The formerly failing native
memory-profile groups now pass with the adjacent `nillinker` build target.
The final independent replay is `github-applications-43135334578/replay-report.json`,
SHA-256 `c362343a73c2adc1a567113a4a0a718306f72889fade367897625128775eb129`.

## Windows executable replacement

The fresh `9fb6c04` Windows suite completes all 589 registrations with
586 passes, two headless desktop skips and a different failure in
`ny.global-contract`: after running `control.exe`, the next successful native
link cannot replace it because `MoveFileExW` reports access denied. The fixture
subsequently cleans its directory successfully; the particular lock holder was
not captured, so it is not attributed to a specific process or scanner.

The compiler already retries access/sharing/lock failures up to fifty times
with a 20 ms delay, but the standalone linker made only one attempt. Both now
use the same helper and preserve the existing output on persistent failure.
The helper neither removes the destination first nor changes its permissions.
This matches Windows' documented
[file-sharing contract](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew):
an open handle without delete sharing prevents rename until that handle closes.
Four relevant Linux CTests pass after the refactor. The first local invocation
had one unbuilt process-fixture executable; that incomplete result is retained
alongside the complete rerun after building it.

At `9fa354d`, strict MSVC (`/W4 /WX /O2`) passes all 46 checks against real
Windows handles: temporary and permanent sharing failures, read-only output,
missing source, exact binary payload preservation, error clearing and cleanup.
The single-attempt mutant fails precisely at the transient-release success
assertion. Eight raw streams and cleanup were independently verified under
`atomic-publication-v1/replay.json`; the original outer wrapper's
`released`/`release` diagnostic wording mismatch is preserved and reconciled
against those native exit results. The clean `9fa354d` compiler/linker build
then passes four exact focused CTests: atomic replacement, global contracts,
the native object pipeline and host-object linking. Their raw reports and
cleanup are verified in `sdk-9fa354d48575/replay-publication-focused.json`.
A corrected complete SDK still needs qualification.

## Intel discovery deadline and SDK build limits

The completed Intel `1efbe66` SDK also records 583 passes, four failures and
one ELF-only execution skip from 588 registrations. Its AppKit desktop tests
pass; the fourth failure instead comes from tool discovery. The first
adjacent-tool `nilbuild check` succeeds, then the first VM `nilbuild run`
exceeds the fixture's 60-second child deadline. This is not its 180-second
CTest limit or the workflow's 35-minute suite limit. The older failure did
not retain the child's partial output, so its internal active phase is unknown.

Only the discovery fixture's two real Darwin project runs now receive the
same 180-second VM allowance as the project/editor fixtures. Other commands
retain 60 seconds. Timings and captured timeout output are retained, with a
600-second Darwin aggregate CTest guard. The Mac application workflow admits
discovery as its fifth SDK contract, for 21 total platform contracts, with a
bounded 30-minute combined step. The actual Linux discovery contract passes
all 21 checks; hosted Darwin follow-up remains required.

The Linux SDK builder also passed a bare `--parallel` to CMake, overriding
`CMAKE_BUILD_PARALLEL_LEVEL=4`. A private empty-project control demonstrates
Make receives unbounded `-j` with that option and `-j4` without it. Removing
the bare option restores the documented caller-controlled build limit.
Both actual driver commands and raw results are retained under
`sdk-parallel-control`. Interrupted local build logs remain preserved; resumed
qualification uses a bounded transient user service and does not replace an
accepted SDK.

## SQL provider environment scope

The full Mac source/SDK workflows previously exported SQL's
`DYLD_FALLBACK_LIBRARY_PATH` to every process, including the builder. That changes
native dependency selection and correctly prevents cache reuse under the modeled
default profile. The workflows now carry an inert test declaration and apply it
only to PostgreSQL server/client and language execution processes. Compilation,
assembly and AOT linking keep their ordinary environment; JIT explicitly remains
a combined compilation/execution process. External callers without the declaration
retain their ambient environment. Conflicting declared and ambient DYLD settings
fail explicitly rather than being silently cleared.

The direct PostgreSQL client CTest receives its path only through its own test
property. Four environment controls inspect all six route branches; seventeen
isolated-server controls, fifteen SDK controls and the existing route/consumer
controls pass. A real bounded Linux SQL smoke still passes all six routes.
Actual Mac source and SDK campaigns must confirm the new provider scope.

## Preserved web handoff

The initial interrupted web snapshot contained the listener-to-engine replacement, template
cache changes, static-file changes, status/main/test edits and four new template
fixtures. Its existing 330 Matrix checks pass. Private, bounded loopback checks
against copies of that source exposed gaps the current unit suite misses:

- Invalid Content-Length and unsupported Transfer-Encoding can be accepted.
- Pipelined requests lose the second request when surplus input is discarded.
- Range processing precedes conditional checks, ignores If-Range and applies
  to HEAD; the observed responses were incorrect 206 results.
- A symlink within a copied public directory could serve an owned marker outside
  that directory. Root-constrained file access needs an actual filesystem boundary.

All diagnostic servers were owned, stopped and reaped. The web source was not
changed by this audit. At that snapshot its README still described 272 checks
and the deleted serial listener; CI audited source paths without executing its
runtime suite. These are historical observations, not evidence of IIS/Apache
equivalence.

A subsequent read-only check found separate work had advanced the web repository
to a clean `41eb756`: `9183727` commits the event loop, `c175582` adds logging and
HTTP dates, and `41eb756` adds configuration and virtual hosts. Its README now
reports 653 checks. Those changes are preserved; the older 330-check snapshot
and its reproductions must be checked against this new source before making
further web changes. The README's remaining-parity paragraph still lists logs,
configuration and virtual hosts as absent despite documenting them elsewhere.

The clean `41eb756` handoff has now been checked against the sealed Linux
`9fa354d` interpreter: all 653 existing checks pass from a disposable source
copy, with four raw streams verified, all 49 repository files and all 268 SDK
files/modes unchanged. Current code still contains the four historical
mechanisms: permissive length/transfer framing, discarded pipelined surplus,
Range handling before conditionals without If-Range/HEAD gating, and lexical-only
static paths. These are code-level findings at the new revision, not fresh HTTP
network reproductions. The existing `read_regular_bytes` protects the final
path component only and cannot by itself establish root containment through
intermediate directories. No diagnostic server was started; the existing suite's
bounded loopback bind/poll/close case completed normally.
Evidence `web-readonly-41eb756/handoff.json`, SHA-256
`092f5c6f1605e2e58620480c2d447f9221fcf94da3c0be2df3989b175a43b2f5`,
retains source lines and focused follow-ups. Runtime CI currently remains absent;
the web workflow runs source audits. Framework platform acceptance remains the
first priority before changing this consumer.


## Current installed Linux closeout and Mac source follow-up

The private Linux SDK at exact `9fa354d48575378065f291c6208148c5e18ab61a`
completes all 590 source CTests with no failures or skips. After relocation to
`NIL SDK space λ`, all 1,184 installed consumer routes pass: 148 retained route
reports supply 888 independently replayed results and 1,628 command records;
296 native object/link executions are validated by the pinned driver. Installed
acceptance also passes 146 debugger checks/15 sessions, live execution
checkpoints, twelve PostgreSQL source/binary routes, sixteen timestamp route
observations with 100 repetitions each, twenty GTK desktop/input cases,
sixteen Unicode byte-file routes, six ordinary I/O observations and actual
relocated `libnil.so` loader/ABI identity.

All 88 VS Code 1.144.1 host groups pass with all nine modules, no exclusions and
the full-suite completion sentinel. The launcher uses an empty private profile,
isolated X11 display and D-Bus, private XDG roots and a launcher-only
`--password-store=basic` option. All 78 source fixture pins remain unchanged;
no test assertions or product binaries were changed. The 267 payload files and
manifest preserve their bytes and modes, the original private install paths
are absent, and all six owned service groups are stopped with no remaining PID.
The composite report is `linux-sdk-9fa354d/linux-private-closeout.json`, SHA-256
`a8d1a5dec3a5e96b0e5d0a717090723a72ccd878fa4a65ed3ca1abfcfd2b13d2`.

Failed attempts remain recorded: PostgreSQL's long private Unix-socket path
exceeded its 107-byte bound; a remaining-only retry uses a short owned temporary
path. An editor launch inherited Wayland without a usable display. A later
attempt timed out during its first NILData open/show/language check, before any
group completed; this was not a demonstrated language-ID mismatch, and observed
portal/keyring activation is not established as its cause. The isolated X11
launch then passes the unchanged assertions. Canonical successful-test cleanup
intentionally removes generated products; independent replay checks their
retained command/result metadata rather than claiming to hash absent files.

Both hosted Mac architectures complete the expanded 21-contract application
campaign at feature head `eca43f1`, tested PR merge
`3c677f7145f083fc228d09371a6f92976ebf81b8`
([run 43141540789](https://github.com/exec-software/exec-nil-framework/actions/runs/43141540789)).
The exact inventory is five SDK/project contracts, six debugger contracts,
eight native contracts and two incremental contracts. Four real desktop DAP
sessions and all 88 VS Code 1.146.0 groups also pass per host, with all nine
modules and no exclusions. All 620 evidence hashes verify. Report
`github-applications-43141540789/replay-report.json` has SHA-256
`1f14c9689ef8ec563854cb3f80a0b4b2d491411a028930973942b8b2285f7d10`.
This resolves the previously open Intel discovery deadline follow-up. Original
full-suite failures stay preserved; this is supplemental current-source
acceptance, not a rewritten full source report or a replacement SDK.

## Windows installed desktop and library access

The exact `9fa354d` Windows candidate completes 590 registered source tests:
588 pass, none fail, and `nilagent.window-discovery`/`nil.ui-display` skip in the
headless session. Eleven root tools, eighteen library modules and 69 compiled
documents are staged. The relocated package then passes all 1,184 consumer
routes, 146 debugger checks/15 sessions, five execution-checkpoint sessions,
twelve PostgreSQL source/binary routes, Unicode/ordinary I/O, timestamps and
runtime hooks. Independent replay verifies 442 installed evidence files and
28 raw phase streams. Installed report SHA-256:
`bc9d27c6f6c451f8ce4bcb5c1ff5b988a81c7fc577e688a3138952aba9071d23`.

Nathan's interactive session initially stops at read-only preflight: it can
read 166 of the 268 SDK files, with all 102 missing files under `nillib`.
Nil/admin can read and hash all 268 unchanged; no readable payload differs.
The compiled library root retains the protected Owner Rights/System/Admin DACL
from Python 3.15's `TemporaryDirectory`, because packaging renamed that private
compilation root directly into `nillib`. Its descendants inherit the restriction.
This is a real cross-account packaging defect; same-user acceptance did not
exercise it. The analogous private POSIX root mode also needs correction.

Only this private candidate's `nillib` received inheritable Nathan read/execute
access, with before/after ACLs and hashes retained under `desktop-access`.
All 268 payload bytes and stat modes remain unchanged; Windows DACLs did change
and are not described as unchanged by the payload seal. Both initial preflight
failures remain preserved. No accepted SDK, original checkout or machine-wide
ACL was changed.

With that explicit access preparation, Nathan passes twenty VM/native UI cases
(ten per source/binary library root), 217 provider and 235 lifecycle checks per
root, window discovery, all 88 editor groups/all nine modules/no exclusions and
four naturally exiting desktop DAP sessions. Extension unit tests pass 161
checks with one POSIX-only skip. All 65 desktop evidence files and sixteen raw
streams verify and the owned task is removed. Desktop report SHA-256:
`c50b32b8f7dd000d3c5159c19cf1c69c19c44057ad76c5305c40c3ca36efa51d`.
The previously headless display surfaces have now executed successfully; their
original source-report skips remain intact. The permanent packaging correction
requires its own tests before claiming default cross-account access.


## Published library access correction

At `3cc2515`, `compile_standard_library` retains its private compilation directory and
copies the fully checked outputs into fresh destination entries. Each published
directory/file inherits the destination's normal access policy; it copies no
private mode/DACL metadata and does not call chmod or edit ACLs. Inventory
rejects symlinks and special files before creating `nillib`. A failed copy
removes only this attempt's new subtree; existing targets are preserved and
original runtime inputs remain until publication succeeds.

The real Linux compiler/interpreter/native mixed-source suite passes thirteen
tests, including umasks 0022/0027/0077, private staging mode 0700, every copied
file's exact hash, an injected second-copy failure, original runtime preservation
and existing file/directory/dangling-symlink refusal. All 46 SDK qualification
controls also pass. The old Linux SDK's root really is 0700; its bytes and modes
remain unchanged. Evidence `package-access-linux-ucvmt82_/report.json`, SHA-256
`c4f33be5b5ec3b3c8849c12eabfa549df0e1306bfd050d38afd459798890f362`.

Actual Windows validation uses the same sealed `9fa354d` tools with only the
packaging/test scripts overlaid in a disposable tree. All thirteen mixed-source
and native tests pass. Limited Nathan reads and hash-matches all 102 published
library files, while access to the original private staging directory is still
denied (errno 13); all 121 published ACL entries inherit destination access.
The one-off task and private stage are removed. All 33 evidence files and eight
raw streams independently verify, and the original candidate remains unchanged.
The first probe expected a Windows-specific error field; actual denied reads
reported `PermissionError` with errno 13. That failed assertion remains retained
alongside the corrected denial check and successful bounded repeat.
Evidence `sdk-9fa354d48575/package-access-ed2765d377/probe-report.json`, SHA-256
`da3fe80b73dd28f92caaf35dd75b87952c49dd417452d90929cd98f74d3538fd`.
The next Mac application campaign also includes the actual mixed-source/native
packaging test, increasing its exact selected CTest inventory from 21 to 22.

## Installed Mac desktop follow-up

[Run 43146638765](https://github.com/exec-software/exec-nil-framework/actions/runs/43146638765)
uses the exact authenticated `1efbe6623445cfcc486b8b7a7794dd112c357224` SDKs from
run `43129959317`, with separately pinned `285aea2` acceptance helpers. It builds
only three test-only desktop helpers; it does not rebuild or substitute product
tools or runtimes. The earlier dispatch `43146568751` had an incorrectly typed
SDK revision and was cancelled before acceptance; the correction is retained.

ARM passes twenty installed UI cases, four desktop DAP sessions with retained
wire frames, 146 installed debugger checks/15 sessions, execution checkpoints
and all 88 VS Code 1.146.0 groups/all nine modules/no exclusions. Independent
replay verifies 213 new evidence files and all 920 original SDK evidence hashes,
alongside exact preservation of all 268 SDK file bytes/modes and helper/editor
inputs. Its desktop report SHA-256 is
`b1f1ba3e46dc0615b5088f27a5f939de1ec7e7617082662a0a14aed95f4c7692`.

Intel passes the same installed UI, desktop DAP, debugger and checkpoint phases,
but the editor stops after 25 passing groups: the `ny: vm project check, build
and run tasks` group reaches its 180-second deadline. Project check completed
in 1,762 ms; project build started without a captured process-end event. The
suite's global task-end listener was already installed, so a late command-local
subscription is not established as the cause. No task-start PID/CPU snapshot
was retained, preventing attribution to compilation, fingerprinting or task
launch. All 211 evidence files verify and input seals remain unchanged. The
failure remains recorded; bounded task lifecycle diagnostics and a same-deadline
Intel follow-up are required before installed editor parity is complete.


The Intel-only diagnostic follow-up
[43149252918](https://github.com/exec-software/exec-nil-framework/actions/runs/43149252918)
passes with helper `fc782892e1655601830ae20273d0b638f3e93501` and the exact same
SDK, prerequisites and group deadlines. All twenty UI cases, four desktop DAP
sessions, 146 debugger checks/15 sessions, checkpoints and all 88 editor
groups/all nine modules/no exclusions pass. Independent replay verifies 213
current evidence files, all 920 original evidence hashes, all 268 SDK files and
unchanged helper/editor inputs. Report
`github-sdk-desktop-43149252918/replay-report.json`, SHA-256
`558c7714f5bc03ecd4ba367abd9cd0681b7755e192438f8ac39c5125b1011d9e`.

The new test-only lifecycle collector records all 31 task executions with four
ordered events each (124 events), and all twelve project command dispatches
complete. Ny VM check/build/run takes 161.171 seconds within 180 seconds:
1.426 seconds checking, 128.389 building and 31.354 running. Ny native takes
241.450 seconds within 300; Matrix VM/native take 56.122/112.599 seconds.
The entire editor completes in 759.587 seconds within 900. The collector changes
no product behavior, test assertions, group inventory or deadline. Its five
controls and all 167 extension unit tests pass. It only diagnoses still-active,
matching workspace task processes on failure, with bounded read-only snapshots.

The original timeout remains unexplained; no task-event-loss defect was
reproduced or fixed. These timings show that first Intel project builds can
consume much of the fixture budget. Future compiler/build-performance work
should measure the loaded-image/shared-cache hashing phases directly before
changing cache identity or timing policy. The passing diagnostic follow-up
completes same-user installed Mac desktop/editor coverage together with ARM's
original pass; it does not qualify shared deployment or a stable release.


## Final portability and evidence-retention checks

The test-only Darwin task diagnostics originally used the current host's path
separator. Their POSIX mock inputs therefore failed two controls on Windows.
At `85ce6ce`, the Darwin-only inspector explicitly uses POSIX path validation;
its real Darwin behavior is unchanged. Actual Windows Node reproduces three
passes/two failures with the old helper, then passes all five controls with the
correction. The complete unit suite records 166 passes, no failures and its one
expected POSIX FIFO skip from 167 registrations. All eight raw streams and 103
copied fixture source pins verify, with no SDK rebuild, real process sampling
or desktop/editor repeat. Report
`sdk-9fa354d48575/diagnostic-unit-6d55b84f08/report.json`, SHA-256
`f47059c8c8d97f0de50a80ce2f367dd04041c8df177ad7884d973ac3b0cbaf13`.

The packaging CTest's successful JUnit output was truncated at the default
1,024 bytes in the Mac campaign. Its exact pinned thirteen-test `--native`
invocation and success exit are retained, but only eight complete named result
lines survive in the raw stream. At `b6ad3a5`, future SDK/project contract runs
retain up to 64 KiB of passing output per test. A real bounded local CTest
control proves the default loses a trailing inventory marker beyond 4 KiB,
while the new bound retains it; neither changes the test verdict. Evidence is
`ctest-passing-output-n20zbgqp/report.json`. Existing truncated evidence is not
rewritten or represented as a full independent replay of thirteen result lines.

Both Linux source workflows at feature `59ffa97` succeed: push
[43148948074](https://github.com/exec-software/exec-nil-framework/actions/runs/43148948074)
and PR [43148956132](https://github.com/exec-software/exec-nil-framework/actions/runs/43148956132),
tested merge `7c552ddcd2261891ce388d04d687d0bc0e5422d6`. Independent replay of all
six authenticated job logs finds identical results: LLVM ON 590 passes/no skips;
LLVM OFF 441 passes and 143 expected disabled-native skips from 584; extension
162 passes/no skips. The workflow emits logs rather than a JUnit artifact.
Report `source-59ffa97-monitor/linux-source-replay.json`, SHA-256
`34d656cc4036d9b8814ad0ed69850cd94fa6146ca7cb1cb494531b1b16fa46dd`.
The corresponding [four-host web-runtime run 43148956115](https://github.com/exec-software/exec-nil-framework/actions/runs/43148956115)
also completes successfully on Linux, Windows, Intel macOS and Apple Silicon.

The web README correction is committed and pushed as `2d3abee`, retaining the
unchanged `41eb756` implementation and its verified 653-check baseline. It
removes obsolete missing-feature claims, records implemented logs/configuration/
virtual hosts, and states the four remaining HTTP/filesystem gaps. Its source
audit CI passes. GitHub accepted the direct master push but reported a bypass
of a PR-only rule; future web changes must use a branch and pull request.


The final expanded Mac source campaign
[43148956133](https://github.com/exec-software/exec-nil-framework/actions/runs/43148956133)
passes on both architectures at feature `59ffa974632dc45c79c50fa04d7f7038db3f79eb`,
tested merge `7c552ddcd2261891ce388d04d687d0bc0e5422d6`. Per host, the exact
inventory is six SDK/project/packaging contracts, six debugger contracts,
eight native contracts and two incremental contracts: all 22 pass without skips.
Four naturally exiting desktop DAP sessions and all 88 exact-name editor
groups/all nine modules/full-suite completion/no exclusions also pass on each
host. All 620 evidence hashes, official artifact ZIP digests/sizes and source/
merge-parent bindings verify. Report
`github-applications-43148956133/replay-report.json`, SHA-256
`39eaacc2159c8ed91a35f418636e862ec13d2a8dd56d4d698d7b449609a0aced`.
The thirteen-test packaging invocation passes with native consumers enabled on
both hosts, subject to the original raw-output retention limit recorded above.

Installed development coverage is now complete across Linux, Windows, Intel
macOS and Apple Silicon for the supported common profiles, using the exact
candidate/helper identities in this notebook. The Intel timeout history and
platform-specific release/deployment limits remain explicit. No accepted SDK,
stable release tag, release service or Lich publication was changed. The web
handoff retains its current code, passing existing suite and four identified
behavior gaps for the next implementation work.
