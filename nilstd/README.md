# NIL Standard Library (`nilstd`)

`nilstd` contains the shared Ny/Matrix general-purpose library sources and native
support code. It is under active development: files in this tree are not
automatically considered supported merely because they are present.

## Layout

```text
nilstd/
├── src/           Active library candidates, grouped by namespace
├── native/        C and C++ interoperability shims
├── tests/         Preserved library tests awaiting runner integration
└── experimental/  Sources quarantined by the compiler-readiness audit
```

The standard namespace root is omitted on disk. For example,
`nil.std.io.console` belongs under `src/io/`. The parser treats legacy
`ny.lang.*` imports as aliases of `nil.std.*`, including qualified names;
there is one module identity and one copy of each implementation. Engine and UI prototypes
are first-party packages rather than standard-library modules and live in
`packages/ny.engine` and `packages/ny.ui.mimic`.

## Current status

The active tree contains 74 sources (68 Ny and six Matrix) organized into 16
logical SDK modules:
`prelude` plus `nil.std.core`, `collections`, `datetime`, `db`, `diagnostics`,
`io`, `math`, `net`, `nydata`, `reflection`, `runtime`, `security`, `system`, `text`, and
`threading`. The SDK packaging gate compiles every logical module with normal
type checking, emits a NIL library (v2, or v4 when carrying classes), and fails
atomically rather than publishing a partial set. Matrix sources retain their
extensions in installed `nilstd` and embedded NIL interfaces, allowing both
frontends to contribute to the same logical module. The aggregate `prelude`
has strict compile/package coverage; the old bypassed per-file screen and
prelude failure are superseded.

That is compile/package evidence, not blanket runtime certification. Focused
fixtures exercise selected surfaces from every active `nil.std` family with
exact output through textual and binary NIL in `nilinterpreter`. The `prelude`
itself has aggregate compile/package coverage rather than a standalone behavior
oracle. The standard library as a whole has no package-wide native-route
certification; the narrower native slices with direct evidence are identified
below.

| Area | Files | Status |
| --- | ---: | --- |
| `src/` | 68 Ny files, 6 Matrix files | Sixteen logical SDK modules. Matrix contributes checked arrays/text spans, stable sorting, reflection values, vector helpers and independent random generators, consumed from both frontends. |
| `experimental/` | 65 Ny files | Design sources excluded from `nilstd`/`nillib` because language semantics, executable providers, or behavioral evidence are incomplete. |
| `tests/` | 8 Ny files | Preserved design-era tests; these are still separate from the registered route fixtures under the repository-level `tests/` tree. |
| `native/` | 14 C/C++ files | Preserved platform/provider shims; source presence alone is neither a build, shipment, nor native-support claim. |

### Active evidence boundary

Finite `average_f64` inputs use compensated subtotals to avoid intermediate
sum overflow. Paired Ny/Matrix fixtures cover the VM and native paths; the
[floating-average contract](../docs/standard-library/floating-averages.md)
describes nonfinite compatibility behavior and the remaining accuracy limits.

Checked text access (`try_substring`, `try_char_at`) and checked decimal
`Convert.try_to_i64` parsing have paired Ny/Matrix VM/native fixtures. See the
[text-span contract](../docs/standard-library/text-spans.md) and
[integer-conversion contract](../docs/standard-library/integer-conversion.md)
for exact bounds, errors and representation limits.

Registered textual/binary interpreter fixtures cover the exercised core
algorithms and conversions, canonical `Option`/`Result` operations, collections,
date/time, SQL construction, diagnostics, file/path/stream I/O, math, HTTP
values, NyData, safe-mode GC, Base64/hex encoding, environment, program
arguments, configuration, text utilities, and timed suspension. They do not
establish every declaration in those families.

The production-file subset includes `read_regular_bytes(path,
maximum_bytes)`, which performs a bounded no-follow regular-file read before
allocating, and `write_bytes_atomic(path, content, replace)`, which stages in
the destination directory and publishes atomically. Its `replace=false` mode
is a race-free no-clobber operation, including for dangling symlinks. The
POSIX publication creates a fresh private `0600` inode even when replacing a
file; existing ownership and other metadata are not preserved. Windows uses
the account's default DACL, and UTF-8 path portability through its narrow host
APIs is not established. Cleanup of a named staging artifact is best effort, so
a rare host cleanup failure may leave an orphan. The textual/binary security
fixture covers exact octets, limits, special files, symlinks, path redaction,
preservation on failure, private POSIX modes, concurrent no-clobber publication,
and dynamically constructed byte arrays. A separate native fixture exercises
exact bytes, caller-bound rejection, no-clobber preservation, replacement, and
invalid-path rejection through both LLVM JIT and AOT. The direct
`native.runtime-abi` test covers the same versioned runtime boundary.

Native support is currently production-targeted and verified on Linux. The
Windows runtime branches and native toolchain path are experimental, and macOS
has not received equivalent route evidence. Windows secure file operations use
narrow host APIs and have weaker parent-directory/namespace guarantees than the
POSIX descriptor-relative implementation. Windows AOT linking also constructs
an external-linker command for `std::system`; do not use untrusted path or
link-library inputs on that route. These restrictions are platform boundaries,
not merely missing test labels.

Byte canonicalization is deliberate: an explicit `as u8` conversion and a
write into `array<u8>` or `ptr<u8>` storage retain the low eight bits, equivalent
to modulo 256 (`-1` becomes `255`, `256` becomes `0`, and `511` becomes `255`).
The native runtime uses one-byte `u8` elements for both arrays and raw buffers.
The interpreter exposes the same canonical values and uses a one-byte
raw-buffer stride, while its general arrays retain a generic VM representation.
The registered route fixture covers conversions, array growth/indexing, raw
pointers, and file round trips on all six routes. This does not imply that every
scalar-width or mixed-arithmetic case is complete.

The checked-array subset is implemented in Matrix under `nil.std.collections`:
`try_get`, `try_set`, `try_fill_range`, `try_copy_range`, and `try_gather`.
In-place operations check the entire requested range before mutation, use
subtraction to avoid bounds-check overflow, and copy overlapping ranges
correctly through shared aliases. Gather validates every requested index
before constructing its separate result array.
Indices and counts are `i32` (`override integer` in Matrix). The exercised
element types are `i32`, `i64`, `u8`, `bool`, `f64`, and NUL-free `text`.
Ny and Matrix consumer fixtures cover the same API. See the
[checked-array contract](../docs/standard-library/index.md#checked-arrays)
for signatures, naming, zero-length rules, and remaining work.

The stable-index subset in `nil.std.core` adds `stable_sort_indices_i32`,
`stable_sort_indices_f64`, and `stable_sort_indices_text`. It returns original indices in key order while
preserving equal-key order in either direction. Floating NaNs sort last in
both directions, and signed zeros compare equal. `try_gather` applies one
ordering to parallel scalar attribute arrays, creating independent array
storage without changing the source. See the
[ordering contract](../docs/standard-library/index.md#stable-index-ordering)
for complexity and edge cases.

The existing `sort_i32`, `sort_i32_desc`, `sort_f64`, and `sort_f64_desc` now
use that Matrix merge implementation while mutating and returning the original
array. They are stable in both directions, take O(n log n) time with O(n)
temporary storage, and avoid recursive quicksort's duplicate-key worst case.
Floating comparison, insertion sort, sortedness, binary search, and extrema
share an ascending NaN-last library order; descending value sorts also keep
NaNs last. This is an intentional change to the earlier library's NaN behavior;
the language's IEEE operators and arithmetic remain unchanged. See the
[numeric value contract](../docs/standard-library/index.md#numeric-value-ordering)
for search equivalence, extrema, and alias behavior.

`sort_text` and `sort_text_desc` now use the shared Matrix merge implementation
as well, preserving in-place aliases and equal-key order in either direction.
Native text `<`, `<=`, `>`, and `>=` use bytewise lexicographic content ordering,
matching the VM over NUL-free strings. Text sorting takes O(n log n) key
comparisons and O(n) temporary array storage; comparison cost also depends on
string lengths. Locale collation, Unicode normalization, and native embedded
NUL text are outside this contract. See
[text ordering](../docs/standard-library/index.md#text-ordering) for search,
index permutations, named insertion sorting, and duplicate handling.

Ny form-feed (`\f`) and vertical-tab (`\v`) decoding is corrected in source
and textual NIL. This repairs whitespace helpers that previously mistook the
letters `f` and `v` for whitespace. Paired Ny/Matrix fixtures use actual control
bytes as an independent oracle; Matrix quoted literals remain raw.

The collection `copy_to` guards in list, queue, stack, hash_set, and dictionary
also use subtraction-safe remaining-capacity checks. This addresses the
extreme-offset defect from the independent validation SDK without changing its
historical copy. Scalar list/queue/stack copies have VM and native coverage;
hash_set/dictionary internals still require aggregate-array native support.

The checked-byte subset adds `Convert.try_hex_to_bytes` and 16 bounded endian
read/write functions under `nil.std.io`. These are implemented in Ny, with
range and window validation in the library rather than hidden in native
helpers. Unlike wrapping `as u8`, checked writes reject out-of-range values
before changing any byte. Unsigned 16-bit values use an `i32` carrier and
unsigned 32-bit values use `i64`; signed `i64` carries all 64 raw bits. Every
operation uses a caller-owned half-open `[offset, limit)` window.

The `checked-hex-runtime` and `checked-binary-runtime` source fixtures pass all
six execution routes: textual/binary NIL interpretation, textual/binary
bundles, LLVM JIT, and LLVM AOT. Supporting native scalar-array, byte-indexed
text length/substr, and panic paths have focused fixtures; this does not
certify the whole standard library or general `Option<userstruct>` native
lowering. Native text remains NUL-free, not an arbitrary byte container.
See [checked binary data and hexadecimal conversion](../docs/standard-library/binary.md)
for signatures, errors, the text boundary, and evidence limits. These two
fixtures also pass from installed `nilstd` and `nillib`, on all six routes and
both standalone assembler/linker pipelines. The published RC is unchanged.

The active I/O tree now also includes pure-Ny canonical base-128 `u32` and
ZigZag `i64` encoding plus bounded, incremental CRC-32. Their
[varint and checksum reference](../docs/standard-library/varint-checksum.md)
defines the strict shortest-form, cursor, mutation, checksum-state, and
non-cryptographic boundaries. Both pass all six build-tree routes and installed
`nilstd`/`nillib` consumers through those routes plus both standalone
assembler/linker pipelines. The varint fixture composes varints, fixed-width
encoding, and CRC in a guarded binary record and detects a one-bit change.

Canonical `Option`/`Result` construction, matching, nested values, selected
methods and iterators, successful unwraps, and exact failure on an invalid
`Option.unwrap` are interpreter-backed. The seven `nil.std.core` `assert_*`
functions likewise have passing and exact-panic fixtures. Postfix `?` and the
separate language-level `assert.*` intrinsic syntax remain unavailable and fail
closed.

The deliberately narrow active boundaries matter: database support is the SQL
builder, networking is HTTP protocol values, security is Base64/hex encoding,
and threading is timed suspension. PostgreSQL execution, transports, broad
threading/concurrency, compression, cryptographic hashes, encryption, and secure random APIs
remain quarantined. Native JIT/AOT parity and platform portability require
separate evidence beyond the narrow slices above. `http.send` and all transport
providers remain quarantined because no runtime dispatcher is shipped.

For `nil.std.system`, “active” does not include subprocess execution or agent
messaging. Those design sources are preserved under
`experimental/system/`, are not imported by the active package, and
are excluded from `nilstd`/`nillib` until real providers and bounded failure
contracts exist.

Some paired FFI declarations and native exports retain `ml_*` symbol names as
a legacy ABI. They are not public tool or component names. Migrate them by
adding matching `ny_*` exports and compatibility aliases before removing the
old symbols; a blind rename would break the bridge contract.

### Threading and concurrency boundary

The active `nil.std.threading` package exposes only `sleep_ms` and `sleep_sec`.
Both lower to `TIME_SLEEP` and have textual, binary, and bundled interpreter
execution coverage, plus LLVM JIT and AOT coverage through the shared native
runtime helper. Timed suspension does not imply task or thread support.

Thread creation, scheduling, atomics, channels, monitors, locks, semaphores,
barriers, wait handles, and tasks live under `experimental`. Their 84
intrinsic declarations have no runtime dispatcher. The three
`ml_concurrency` platform exports are preserved as prototype native shims, but
no build or install target connects them to the SDK. The corresponding legacy
Ny files under `tests/threading` and `tests/concurrency` are
design-era probes, not passing support contracts.

### Database and networking boundary

The active `nil.std.db` package combines deterministic parameterized SQL
construction with a shared SQLite/PostgreSQL provider API: `db_available`,
`db_open`, `db_execute`, row/column/cell access, and explicit `db_release` and
`db_close`. Provider libraries are loaded at runtime; `db_available` reports a
loadable provider, not a reachable PostgreSQL server. SQLite fixtures exercise
the VM, bundles, JIT, and AOT. PostgreSQL integration requires a separate server
and does not inherit SQLite's route evidence. The legacy PostgreSQL wrapper in
`experimental/db` is distinct from this active provider API.

Execution accepts one statement with bounded text parameters and explicit
NULL flags, timeouts, and bounded text results. Handles belong to the current
execution context. PostgreSQL addresses use numeric `hostaddr` or an absolute
Unix socket host; DNS/service-file lookup is outside this POSIX provider slice.
See [the API contract](src/db/database.ny) for exact limits, connection
invalidation, error handling, and transaction responsibilities.

The behavior-backed part of active `nil.std.net` is limited to HTTP
protocol values and request/response builders. `http.send`, HTTP client/server,
NyWeb, socket, WebSocket, IMAP, and SMTP
sources remain under `experimental/`; their intrinsic or native providers are
not dispatched, built, and installed together today.

### Reflection value views

`nil.std.reflection` provides `declaration_info` and `annotation_info` records,
the predicates `is_function`, `is_record`, `is_public_api`, and
`has_checked_semantics`, and JSON diagnostic formatting. The compiler generates
an `analysis_snapshot` module with `count`, `declaration`, `annotation_count`,
and `annotation` accessors. IDs belong to that snapshot; an absent parent is
`-1`, and an unavailable source line is `0`. Records are ordinary returned
values, so changing a local copy cannot mutate the compiler's AST.

The [Matrix analyzer example](examples/analyzer.matrix) reads a generated
snapshot and prints structured diagnostics for temporary public function names.
It is compiled and run explicitly as a regular program. Reflection does not
automatically load plugins or grant compiler mutation access. These are
declaration views; complete expression bindings, control flow, generic
constraints, and automatic editor hosting of custom analyzers are future work.

`diagnostic_json(code, severity, message, source_path, line)` returns one JSON
object. `json_text` quotes NUL-free UTF-8 text and escapes quotation marks,
backslashes, tab, newline, carriage return, form feed, and vertical tab. Other
ASCII controls fail explicitly, matching the generated-source export boundary.
The utilities use Matrix; three private Ny paragraphs supply escaped control
constants without depending on native Unicode-conversion intrinsics. Paired
Ny/Matrix fixtures cover value queries and formatting through all six routes.

## Promotion criteria

Move a module from `experimental/` into `src/` only after it:

1. parses without recovery diagnostics;
2. resolves imports from the canonical standard-library root;
3. passes type checking without bypass flags;
4. links required native shims through an explicit target; and
5. has a focused executable test.

Until those checks exist, describe `nilstd` as an experimental standard
library rather than a complete or stable one.
