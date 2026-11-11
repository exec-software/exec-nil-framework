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

Native socket/byte/entropy parity needs a binary-safe value boundary. The new
VM APIs deliberately carry embedded NUL and arbitrary bytes in `text`; the
current native text ABI and several LLVM operations use null-terminated strings
and `strlen`. Merely registering these intrinsics with that ABI would silently
truncate legitimate network data and entropy. The scalar timestamp operation can
use the existing ordinary-I/O ABI; the byte APIs require an explicit length-aware
design and cross-route tests before native support is claimed.

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
