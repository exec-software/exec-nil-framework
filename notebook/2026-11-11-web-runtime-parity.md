# Web runtime and platform parity — 2026-11-11

The web handoff resumes at framework `dfb7ad7` on `nil-sockets-entropy-bytes`
([PR 38](https://github.com/exec-software/exec-nil-framework/pull/38)) and web
`594579a` plus preserved local changes. The archived originals are under
`atoma/archived-exec-ny-web` and `atoma/archived-exec-ny-webview`. Platform work
comes first; the web changes are not discarded, reset or overwritten.

## Current baseline and remaining boundaries

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

The latest source validation push run
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

The interrupted web tree contains the listener-to-engine replacement, template
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
changed. Its README still describes 272 checks and the deleted serial listener;
CI audits source paths but does not execute its runtime suite. These are the next
web tasks after platform parity work, not evidence of IIS/Apache equivalence.
