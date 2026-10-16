# Standard-library capability ledger

**Status:** observed; September 13 audit with September 26 parity updates.

The native parity work supersedes the
older array and timed-suspension limits below. Historical package-wide
classifications remain narrower than source compilation or SDK inclusion.

## Observation

Strict compilation is necessary, but it is not a support claim. At the start of
this pass the active `nilstd` tree contained 100 Ny sources in 19 logical module
groups and 185 `@intrinsic` declarations. Several families compiled cleanly
while their interpreter dispatcher, optional native provider, or behavioral
tests were absent. A human or agent looking only at an SDK's file inventory
could not distinguish executable APIs from design-preserving declarations.

That ambiguity is especially costly for AI-driven development: a model can
select a plausible API, receive a valid NIL artifact, and discover only at
runtime that the operation falls through to an unavailable foreign library.

## Proposal

Treat standard-library support as route-scoped, machine-readable evidence. A
future SDK manifest should describe each logical module with fields equivalent
to:

```text
module
source_files
compiled_nil
interpreter_provider
native_provider
platforms
behavior_tests
known_gaps
maturity
```

`maturity` should be derived from evidence rather than written as marketing:

1. **experimental** — retained design source, excluded from the active SDK;
2. **checked** — parses, resolves, and type-checks into NIL;
3. **interpreted** — exact behavioral fixtures pass through textual and binary
   NIL in `nilinterpreter`;
4. **native** — the same oracle passes JIT and AOT on a named target; and
5. **portable** — supported platform providers and their failure behavior are
   independently exercised.

No tier implies the next. Optional facilities such as PostgreSQL, TLS, window
control, or hardware acceleration must identify their compiled provider in the
SDK instead of being inferred from source declarations.

## Packaging gate

The current SDK gate correctly requires every active source group to compile
with normal type checking and publishes atomically. The next gate should also:

- reject an intrinsic marked as interpreter-supported when no dispatcher owns
  its exact identifier;
- reject a native-supported claim when its provider was not built into or
  shipped beside the SDK;
- link every claim to a named test and route result;
- preserve explicit compile-only modules without presenting them as runtime
  complete; and
- let editors and agents query capabilities without probing by failure.

This does not require pretending the early standard library is finished. It
makes partial implementation attributable, searchable, and safe to improve.

## First evidence in this cleanup

The initial behavioral fixtures cover collection mutation and lookup,
canonical `Option`/`Result` dispatch, generic trait implementations,
diagnostic logging, NyData parsing/serialization, exact byte-file round trips,
and selected file error contracts through both interpreter NIL formats.
An external consumer adds a production-shaped oracle for arrays, pointer-backed
buffers, structs, enums, bitwise CRC32C, temporal tile coding, and hostile
decode paths.

Remaining modules must be classified by the same standard. Empty namespace
packages, unconnected threading primitives, optional database paths, and
network wrappers are not implementation evidence merely because they compile.

## Threading and concurrency audit

The active `ny.lang.threading` surface is now limited to `sleep_ms` and
`sleep_sec`. A private `time.sleep` intrinsic lowers both calls to the
interpreter's implemented `TIME_SLEEP` opcode; the exact output contract runs
through textual, binary, and bundled NIL. Since September 26, LLVM also lowers
`TIME_SLEEP` through the shared runtime helper; the native-time and threading
fixtures require JIT/AOT success. Only these timed-suspension APIs are native,
not the quarantined concurrency declarations.

Thread creation, scheduling, atomics, channels, interlocked operations,
monitors, mutexes, reader/writer locks, semaphores, barriers, wait handles, and
tasks were moved to `experimental/ny/lang/threading` and
`experimental/ny/lang/concurrency`. Their 84 `@intrinsic` identifiers have no
compiler opcode or interpreter dispatcher. Three `@interop("ml_concurrency")`
declarations have matching platform source shims, but those shims have no
build/install target and are absent from the SDK. An exact compile-rejection
fixture establishes that these names are not part of the active package.

## I/O, math, and security audit

The active surface is five I/O sources (`package`, `console`, `file`, `path`,
and `stream`), five math sources (`package`, scalar math, trig, vectors, and
deterministic random), and three security sources (`package`, Base64, and hex
encoding). All three groups pass normal type checking as complete groups. This
exposed a deceptive math result:
three files began an unterminated `###` block comment immediately after their
imports, so the old “success” had compiled empty modules. Converting those
headings to line comments exposed and allowed repair of the real declarations,
global RNG syntax, mutable approximation state, and cross-file constant use.

`ny.lang.io` remains **interpreted** as a package-wide classification for the
exercised file/path/stream surface. Interpreter-owned opaque handles enforce
read/write modes and checked buffer bounds for open, read, write, seek, flush,
and close. Whole-file byte and text operations have strict argument/error
contracts and a 512 MiB safety ceiling; path extension helpers have runtime
handlers.
`ny.interpreter.byte-io`, `ny.routes.file-stream-runtime`, and
`ny.routes.io-path-file-runtime` exercise exact bytes including 0/127/128/255,
text and binary NIL, all four open modes, partial/error paths, use after close,
negative and oversized spans, EOF, append behavior, missing files, and exact
stdout/stderr.

Two narrower I/O slices are now **native** on the verified Linux LLVM JIT/AOT
host.
`ny.routes.native_cli_text_console` checks `write`, `writeln`, and `write_error`
with exact stdout and stderr while also exercising native text concatenation
and content equality. `ny.routes.native_cli_secure_byte_io` checks bounded
regular byte reads and atomic no-clobber/replacement writes using isolated
route directories. Those tests do not imply native support for the broader
stream, compatibility whole-file, or path-helper APIs, and they are not a
cross-platform portability claim.

The external CLI audit added two security-sensitive APIs rather than silently
strengthening the compatibility operations. `read_regular_bytes`
opens once with nonblocking/no-follow semantics, verifies a regular descriptor
and caller ceiling before allocation, performs an exact read plus EOF and
post-read metadata checks, and returns path-redacted errors.
`write_bytes_atomic` writes an unpredictable, exclusive same-directory staging
file, flushes and closes it, then publishes atomically. `replace=false` uses an
atomic no-clobber operation that rejects every existing entry, including a
dangling symlink; `replace=true` rejects a non-regular entry observed during
preflight and never follows the destination entry. It does not claim namespace
containment if another process can mutate the parent directory. POSIX publishes
a fresh private `0600` inode and does not preserve metadata from a replaced
entry. Windows uses the account's default DACL; its narrow host path APIs do not
yet establish UTF-8 portability. Named staging cleanup is best effort, and a
rare cleanup failure can leave an orphan.

The registered `ny.interpreter.secure-byte-io` fixture proves both textual and
binary NIL, exact octets and success payloads, caller limits, FIFO/symlink and
directory-output rejection, dynamic-size detection, NUL/empty path rejection,
error redaction, preservation on failure, private POSIX modes, `replace=true`
creation, complete single-winner publication under concurrent no-clobber
competition, ordinary-path staging cleanup, and dynamic `array<u8>` output. It
observes final sequential replacement bytes; the atomic-visibility guarantee
comes from the host publication primitive rather than a concurrent-reader
oracle.

The native `nyrt_*` ABI now calls the same secure host implementation for
`read_regular_bytes` and `write_bytes_atomic`. `native.runtime-abi` verifies
the direct C boundary, and `ny.routes.native_cli_secure_byte_io` passes the
public Ny APIs through both JIT and AOT, including exact bytes, a caller-bound
rejection, no-clobber preservation, replacement, and invalid-path rejection.
The pre-commit four-module AOT run also completed real
encode/inspect/decode publication; its clean manifest-backed hostile-path JIT
and AOT differential remains pending the next SDK refresh. Windows uses narrow
host paths, reparse checks, and `MoveFileEx`; UTF-8 portability is unverified
and its namespace guarantees are explicitly weaker than POSIX
descriptor-relative staging. The Windows native route remains experimental,
and its AOT linker passes a constructed command string through
`std::system`/`cmd.exe`; untrusted path or link-library inputs are outside its
supported threat model. macOS likewise has no equivalent native route evidence.

The byte-value rule underneath both implementations is deliberate and now
route-pinned. Explicit `as u8` conversion and writes into `array<u8>` or
`ptr<u8>` retain the low eight bits, equivalent to modulo 256: `-1 -> 255`,
`256 -> 0`, and `511 -> 255`. Native arrays and raw buffers use one-byte
elements. The interpreter preserves the same canonical values and a one-byte
raw-buffer stride, while general arrays retain their generic VM representation.
`ny.routes.u8-canonicalization` covers casts, array growth/indexing, pointers,
and file round trips on all six routes. This does not upgrade the still-partial
scalar-width and mixed-arithmetic ABI.

`ny.lang.math` is **interpreted** for the exercised scalar, trigonometric,
hyperbolic, deterministic-random, range, and vector subset. Bounds checks now
reject inverted clamp/range arguments, approximation includes the epsilon
boundary and rejects negative epsilon, and integer range sampling uses two
MINSTD draws plus rejection sampling so the entire i32 domain is reachable
without modulo bias. `ny.routes.math-runtime` checks exact results through text
and binary NIL, and `ny.routes.math-invalid-range-runtime` locks the
inverted-bound panic contract. The generator is deterministic and explicitly
not suitable for security.

A narrower imported scalar-math slice is also **native** on the verified LLVM
host. `ny.routes.native-math-intrinsic-module` executes `sqrt(81)`,
`pow(2,10)`, and `cbrt(27)` through all six routes with exact integer-observed
results. This proves the LLVM intrinsic/libm provider boundary for those three
operations; it does not upgrade the package-wide math, random, vector, or
platform-portability classifications.

`ny.lang.security` now exposes only evidence-backed encoding. Portable strict
Base64 encode/decode and byte-to-hex conversion are **interpreted** via
`ny.routes.security-base64-runtime`, including all octet boundaries, empty and
padded inputs, invalid alphabet/placement, and non-canonical pad bits. The
hash, crypto, secure-random, and compression declarations were moved to
`experimental/` because their preserved C++ providers are not built or
installed, return C++ library objects across a nominal C ABI, and do not match
the library/symbol interpretation of the current one-argument `@interop`
annotation. Additional defects include a Ny-i32/native-vector mismatch in
`random_int`, a non-cryptographic integer generator, ambiguous empty-vector
crypto failures, and unauthenticated raw ECB helpers. None of these preserved
prototypes constitutes security, compression, or native support until an
ABI-safe result-bearing provider and exact route tests ship.
`ny.routes.reject-unbacked-io-security` proves representative gzip, SHA-256,
secure-random, and AES names do not leak back into the active packages.

## Database and networking audit

The active database and networking families now contain four Ny sources and no
foreign-call declarations. `ny.lang.db` retains only parameterized SQL
construction; PostgreSQL connection/result declarations and their 18 optional
`db.*` intrinsics moved to experimental. The current CMake cache has neither the
libpq headers nor library, and the SDK has no route-scoped provider marker.

`ny.lang.net` retains pure HTTP protocol values and request/response builders.
The `http.send` intrinsic remains quarantined because it has no dispatcher. The
preserved HTTP server, NyWeb, and socket native sources have no build/install
target, so their 40 `@interop` declarations and all dependent transports remain
experimental.

Exact SQL/parameter and HTTP-value contracts passed through textual and binary
NIL before registration as `ny.routes.db-query-builder-runtime` and
`ny.routes.http-values-runtime`. A compile-rejection contract proves that the
active packages do not export `db_connection` or `http_client`. The audit also
found and drove repairs for two compiler limitations relevant to API design:
fluent calls after static factories now retain their receiver type, and an
enum-associated function is dispatched only after the member name is verified
not to be a declared variant. The fixtures preserve both natural forms as
regression coverage.

Remaining pure-library gaps are explicit: SQL identifiers and raw join
fragments are trusted rather than validated, structurally incomplete statements
can still be built, the HTTP status set is not exhaustive, and neither family
has a native-route behavior oracle.

## Core, runtime, and system audit

The active `ny.lang.core`, `ny.lang.runtime`, and `ny.lang.system` packages pass
normal type checking as complete package groups. There is still no
package-wide native parity claim. Registered fixtures exercise the broad named
contracts through both textual and binary NIL in `nilinterpreter`, while a
smaller native subset now has independent JIT/AOT evidence: checked i32 parsing,
i32/i64 text formatting, concrete `Result` matching, and exact program
arguments. `ny.lang.runtime` remains interpreter-only for its exercised GC
contract.

`ny.lang.core` now has executable strict conversions for signed integers,
finite decimal values, booleans, bases 2 through 36, Unicode scalars, and byte
hex encoding. Malformed input, range overflow, non-finite decimal text,
invalid bases/scalars, precision, and odd-length byte hex fail with exact
errors. This full surface remains an interpreter claim. The native subset is
deliberately narrower: `ny.routes.native_cli_numeric_conversions` verifies
`Convert.try_to_i32`, `Convert.i32_to_text`, and `Convert.i64_to_text`, including
success and failure `Result<i32,text>` matches, through JIT and AOT. Integer
algorithms reject undefined or unrepresentable cases rather than overflowing
silently. Empty sorting aggregates return `Option`, and the shuffle avoids
signed-overflow state. Canonical nested `Option`/`Result` values, selected
combinators, concrete one-element iterators, sorting, faults, and typed scalar
assertion helpers have exact interpreter fixtures. An independent imported
generic free-function fixture protects the specialization path; the
Option/Result and I/O vector fixtures remain companion regression gates.

The interpreter now rejects equality and ordering for aggregate or foreign
value categories without a defined executable comparison contract. Struct,
enum, and array equality plus struct ordering each have an exact negative
fixture, while numeric, text, and boolean comparisons remain covered. Recursive
value equality/ordering is still a language-design gap; it is not simulated by
handle identity. Generic iterator traits and nested-shape `Result.transpose`
also remain out of the active contract because the current type system cannot
express and resolve them reliably.

`ny.lang.runtime` exposes a safe-mode mark/sweep collection contract with
rooted nested arrays/enums, explicit critical-section suppression, and logical
payload counters. The accepted generation values all request the same full
collection: generational collection is not implemented, and counter values do
not include host allocator/container overhead. Explicit collection outside
safe memory mode fails closed.

That paragraph describes `nilinterpreter`. Safe LLVM execution currently uses a
separate invocation-scoped `nilruntime` arena: compiler-created aggregates and
strings are reclaimed at `nyrt_context_end`, while logically released raw
buffers remain tombstoned until that boundary. It must not be described as the
interpreter's tracing collector or as a long-lived object-lifetime system.

`ny.lang.system` distinguishes missing from explicitly empty environment
values, validates mutation, bounds and sorts enumeration, reports cwd/host/user
identity, preserves file errors in configuration loading, applies last-key-wins
duplicate handling, and provides checked program arguments and process exit
codes. Environment fixtures use only a controlled test key. The argument
subset is now native: `ny.routes.native-system-arguments-runtime` supplies an
ordinary value, an empty value, and an option-looking value as explicit argv
and checks `arg_count`/`arg` exactly through JIT and AOT. Other system services
remain scoped to their interpreter evidence. Subprocess and agent APIs were
removed from the active package because their former bodies returned invented
success without providers; their design sources remain under
`experimental/ny/lang/system/` and are excluded from `nilstd`/`nillib`.
