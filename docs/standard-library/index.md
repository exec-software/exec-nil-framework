# Standard-library status

Ny and Matrix share `nilstd`, one standard library compiled to NIL. Its 76 active
sources (69 Ny and seven Matrix) form 16 logical SDK modules, including `prelude`.
Support remains evidence-scoped. Focused fixtures exercise `nilstd` and `nillib`
through textual/binary NIL, bundles, LLVM JIT and AOT, and standalone
assembler/linker pipelines. Local SDK qualification records the exact source
snapshot and does not replace the published RC or active SDK. Runtime support
is claimed only for the surfaces with focused route fixtures, and native
support requires separate evidence.

## Canonical layout

The [text-pattern reference](text-patterns.md) defines readable declarations,
bounded conventional regex, captures, byte spans, match iteration, whole-match
collections, splitting and literal replacement.
The [query reference](queries.md) defines eager filtering, stable ordering,
pagination, projection and the Matrix-authored collection terminals.

The [vector norm reference](math-vectors.md) describes stable two- and
three-component lengths, finite-value checks, and checked normalization shared
by Ny and Matrix.
The [random-generator reference](random.md) defines independent mutable
streams, reproducible seeds, cloning, and bounded integer/floating sampling.
The [checked text-span reference](text-spans.md) defines exact byte ranges,
empty-span results, and overflow-safe compatibility slicing.
The [checked integer-conversion reference](integer-conversion.md) covers
decimal parsing and matching `Result` behavior on VM and native routes.
The [floating-average reference](floating-averages.md) defines bounded finite
averaging, nonfinite compatibility behavior and numerical accuracy limits.

```text
nilstd/
  src/           Ny and Matrix sources organized as 16 logical modules
  native/        14 preserved C and C++ interoperability shims
  tests/         8 preserved design-era library tests
  experimental/  65 quarantined Ny sources excluded from the SDK
```

The active tree uses neutral family folders: `nil.std.io` lives under
`nilstd/src/io`. Public imports use `nil.std.*`; legacy `ny.lang.*` spellings
remain aliases of the same module identities.

The active logical modules are `prelude` and `nil.std.core`, `collections`,
`datetime`, `db`, `diagnostics`, `io`, `math`, `net`, `nydata`, `reflection`, `runtime`,
`security`, `system`, `text`, and `threading`. Quarantine is broader than parser
failure: an experimental source may compile yet still lack complete language
semantics, an executable provider, or behavioral evidence.

The distributable SDK uses a separate, consumer-facing layout:

```text
nilstd/  source modules, preserving the repository-relative hierarchy
nillib/  matching strictly compiled binary NIL modules
```

For every source under `nilstd/path/module` (`.ny`, `.matrix`, or `.mat`), a complete SDK contains
`nillib/path/module.nil`. `manifest.json` records both inventories and hashes.

First-party engine and UI experiments are intentionally separate from the core
library:

```text
packages/ny.engine/
packages/ny.ui.mimic/
```

Other opt-in experiments, including `ny.godot`, also live under
`packages`. `nilaix` is a separate UI/AI integration sandbox.

## Current module families

The active tree includes these deliberately scoped families:

- core values and algorithms, collections, text, math, date/time, and NyData
- Matrix-authored checked array access, bounded fill, overlap-safe copy, and gather
- stable numeric and text index ordering for parallel attribute arrays
- independent deterministic random generators with cloning and finite floating ranges
- console/file/path/stream I/O and diagnostics
- checked hexadecimal decoding and bounded fixed-width binary reads/writes
- canonical base-128 integers and bounded, incremental CRC-32
- environment, program-argument, and configuration helpers plus safe-mode GC
- deterministic SQL construction and HTTP protocol values
- Base64/hex encoding under `nil.std.security`
- timed suspension on interpreted and native routes under `nil.std.threading`

Console output, `nil.std.io.read_line`, and `flush` support the interpreter,
native executables, and LLVM JIT. Line input preserves bytes except for the
trailing LF; an empty line and a final nonempty line without LF are valid.
EOF and input errors fault explicitly. On POSIX, CR is preserved. Native text
uses a NUL-terminated ABI and rejects embedded NUL input; the interpreter can
retain that byte. Input publishes pending console output before reading, and
`flush` publishes standard output explicitly. Native input text is managed by
the runtime and included in Aperture's text allocation measurements.

For untrusted file/container input, `nil.std.io.read_regular_bytes` accepts an
explicit byte ceiling and rejects the final-component symlink, special file,
short/growing read, and observed metadata change before returning bytes.
`write_bytes_atomic` stages and flushes bytes in the destination directory,
then atomically publishes them; `replace=false` cannot clobber an existing
entry (including a dangling symlink), while `replace=true` only replaces a
regular entry observed during preflight and never follows the destination.
Callers requiring namespace containment must control the parent directory,
because a path entry can change after preflight. These errors are path-redacted.
On POSIX every publication is a fresh private `0600` inode, including
replacement; prior ownership, mode, ACLs, and extended attributes are not
preserved. Windows uses the account's default DACL, and UTF-8 portability through
its narrow path APIs remains unverified. Named staging cleanup is best effort,
so a rare host cleanup failure can leave a `.ny-write-*` orphan. Publication is
visibility-atomic but does not promise parent-directory durability after a
sudden power loss.

These two secure whole-file APIs now have a native provider in the versioned
`nilruntime` ABI. `native.runtime-abi` tests the C boundary directly, and
`ny.routes.native_cli_secure_byte_io` exercises the public Ny wrappers through
LLVM JIT and AOT with exact bytes, an explicit read ceiling, no-clobber
preservation, replacement, and invalid-path rejection. This is not a native
claim for stream handles, the compatibility whole-file APIs, or the remaining
path helpers.

The supported native platform boundary is currently Linux. Windows runtime and
linker branches exist but remain experimental; macOS has not received
equivalent route verification. In particular, Windows uses narrow host file
APIs with weaker namespace guarantees, and Windows AOT linking builds a command
string for `std::system`. Treat untrusted paths and link-library arguments as
unsafe on that compilation route until it uses an argv-based process launch and
has equivalent hostile-path tests.

Ny's byte boundary is intentionally wrapping rather than range-asserting.
Explicit `as u8` conversion and writes to `array<u8>` or `ptr<u8>` retain the
low eight bits (modulo 256), so `-1`, `256`, and `511` canonicalize to `255`,
`0`, and `255`. Native arrays and raw buffers use one-byte elements. The
interpreter preserves the same values and a one-byte raw-buffer stride, while
general interpreter arrays use a generic VM representation. The all-route
`ny.routes.u8-canonicalization` fixture includes casts, arrays, raw pointers,
and secure file round trips. This rule does not certify every scalar-width or
mixed-arithmetic case.

For checked parsing and encoding, `Convert.try_hex_to_bytes` rejects malformed
ASCII hexadecimal input without returning partial bytes. The 16 `nil.std.io`
endian helpers validate a half-open `[offset, limit)` window and reject a
failed write before any mutation. Their unsigned 16/32-bit formats use signed
`i32`/`i64` carrier types; signed `i64` preserves all 64 raw bits. Both library
implementations are Ny code, backed by narrowly scoped native array/text/panic
mechanisms. The `checked-hex-runtime` and `checked-binary-runtime` source
fixtures pass textual/binary NIL, both bundle routes, LLVM JIT, and AOT using
the build-tree tools, and from installed `nilstd`/`nillib` with both standalone
assembler/linker pipelines. This is not a claim that general
`Option<userstruct>` native lowering works. Native text remains
byte-indexed and NUL-free. The
[checked binary and hexadecimal API reference](binary.md) defines the exact
contracts and remaining limits.

The active I/O tree also contains pure-Ny canonical varint and CRC-32 APIs.
Their [varint and checksum reference](varint-checksum.md) records the wire
format, strict shortest-form policy, window and mutation guarantees, and
non-cryptographic checksum boundary. Both pass all six build-tree routes and
installed `nilstd`/`nillib` consumers through those routes plus both standalone
assembler/linker pipelines. A guarded binary-record fixture combines varints,
fixed-width encoding, and CRC, including detection of a one-bit change.

Each active `nil.std` family has at least one exact-output fixture through
textual and binary NIL, but those fixtures establish selected behavior rather
than every declaration in a family. The aggregate `prelude` has strict
compile/package coverage, not its own standalone behavior oracle. Network
transports, JSON, broad concurrency/threading, compression,
cryptographic hashing, encryption, and secure random generation remain experimental.
`http.send` and all network transports remain quarantined because no runtime
dispatcher is shipped, and active `nil.std.system` does not include subprocess
execution or agent messaging.

The [database and networking capability boundary](database-networking.md)
records the audited split between active deterministic builders and quarantined
transport providers.

## Checked arrays

`nil.std.collections` includes five generic operations implemented in
[Matrix](../../nilstd/src/collections/array_checked.matrix).
Ny imports them with `import nil.std.collections.*`; Matrix uses
`read everything from nil.std.collections`. Their names below use Ny spelling;
Matrix callers may replace underscores with hyphens.

| Function | Result |
| --- | --- |
| `try_get<T>(items: array<T>, index: i32)` | `Option.Some(value)` for an existing element, otherwise `Option.None`. |
| `try_set<T>(items: array<T>, index: i32, item_value: T)` | `true` after replacing an existing element, otherwise `false`. |
| `try_fill_range<T>(items: array<T>, offset: i32, count: i32, item_value: T)` | `true` after filling the selected range, otherwise `false`. |
| `try_copy_range<T>(source: array<T>, source_offset: i32, destination: array<T>, destination_offset: i32, count: i32)` | `true` after copying the selected range, otherwise `false`. |
| `try_gather<T>(items: array<T>, indices: array<i32>)` | `Option.Some(array<T>)` containing the selected elements in index order, otherwise `Option.None` if any index is invalid. |

Offsets and counts use signed 32-bit positions (`override integer` in Matrix),
matching the array runtime. A range requires `0 <= offset <= length` and
`0 <= count <= length - offset`. Copy validates both windows before reading or
writing any element. A zero count at the array end is valid; negative counts
or offsets past the end are invalid even when the count is zero. Validation
uses subtraction after checking the offset, so extreme offsets cannot wrap an
`offset + count` check.

Invalid operations leave every input array unchanged. Successful in-place writes affect only
existing selected elements and are visible through aliases; they never resize
or allocate a replacement array. Copy has memmove-style overlap behavior:
copying a range within the same array, or through another alias of it, preserves
the original source values in either direction. The source and destination may
also be independent arrays. Fill and copy use constant auxiliary storage and
linear time in the number of elements.

Gather validates all indices before constructing a new result array. Subsets
and repeated indices are valid; an empty index array returns `Some` containing
an empty array, even when the input is empty. A negative index or an index at
or beyond the input length returns `None`. Gather never modifies either input.
Its result has independent array storage and shallow element copies, so it is
not a recursive clone. Its time and result storage are linear in the index
count. This contract lets several attribute arrays use the same ordering.

For a Matrix caller:

```matrix
book checked-example
read everything from nil.std.collections

main returns number begin paragraph
    value pixels as array of override integer is [1, 2, 3, 4]
    say try-copy-range with source being pixels, source-offset being 0,
        destination being pixels, destination-offset being 1, count being 3
    say pixels at 3
    give 0
end paragraph
```

The output is `1` then `3`; the array becomes `[1, 1, 2, 3]`. Ny calls the same
function as `try_copy_range(pixels, 0, pixels, 1, 3)`.

The scalar test slice covers `i32`, `i64`, `u8`, `bool`, `f64`, and NUL-free
`text`. The [tuple-array contract](../../tests/tools/tuple_array_contract.py)
adds generic construction, indexed/intrinsic access and overlapping checked
copies with exact tuple schemas. Copies preserve tuple values and contained
reference identities. Native record, enum and nested-array element arrays
remain unsupported. Checked byte stores retain the existing `u8` conversion
rules; these APIs validate indices and ranges, not numeric conversions.
Raw scalar array accessors retain their zero/no-op bounds behavior. Raw tuple
reads outside the array fail because no default tuple exists; use checked
operations or validate bounds before reading.

The related `copy_to` guards on `list`, `queue`, `stack`, `hash_set`, and
`dictionary` now check remaining destination space by subtraction. Invalid
copies still panic with the existing collection-specific messages before any
write. This fixes the extreme-offset overflow identified by the independent
3D validation project. Its copied SDK remains a historical test subject.

Further array work remains: real vector reservation/resizing, strided buffer
views with ownership rules, and native record/enum/nested-array element layouts.
None is implied by these helpers.

## Stable index ordering

`nil.std.core` supplies three Matrix-authored functions in
[sorting_indices.matrix](../../nilstd/src/core/sorting_indices.matrix):

| Function | Result |
| --- | --- |
| `stable_sort_indices_i32(keys: array<i32>, descending: bool)` | An `array<i32>` containing each original index exactly once, ordered by its integer key. |
| `stable_sort_indices_f64(keys: array<f64>, descending: bool)` | The equivalent ordering for floating-point keys, with the NaN policy below. |
| `stable_sort_indices_text(keys: array<text>, descending: bool)` | The equivalent ordering for text keys, using bytewise lexicographic order. |

All three leave the key array and its aliases unchanged. Equal keys retain their
original index order in ascending and descending results. Floating-point
infinities follow numeric ordering; positive and negative zero compare equal.
NaNs come last in either direction and retain their own original order.
The numeric value sorts below use the same stable ordering.

The implementation uses iterative stable merge sorting: O(n log n) key
comparisons and O(n) auxiliary storage, without recursion or comparison callbacks. Merge
boundaries and run-width growth use the remaining length to avoid signed
index overflow. The output is a fresh permutation; empty input yields an
empty array. Numeric comparisons take constant time; text comparisons also
depend on the compared byte lengths. Native support in this slice covers
integer, floating, and NUL-free text keys, without arbitrary comparator callbacks.

Apply the resulting indices to parallel attributes with `try_gather`:

```matrix
book ordered-scene
read everything from nil.std.core
read everything from nil.std.collections

main returns number begin paragraph
    value depths as array of floating is [2.0, 4.0, 4.0]
    value labels as array of text is ["near", "far-a", "far-b"]
    value order is stable-sort-indices-f64 with keys being depths,
        descending being true
    value selected is try-gather of text with items being labels, indices being order
    value ordered-labels is selected.unwrap with nothing
    say ordered-labels at 0
    say ordered-labels at 1
    say ordered-labels at 2
    give 0
end paragraph
```

The order is `[1, 2, 0]`, and the output is `far-a`, `far-b`, then `near`.
The two equal-depth entries stay in their original order. A shorter attribute
array can reject that ordering through `None`; callers must handle that result
before unwrapping it. The paired Ny and Matrix route fixtures cover ordering,
stability, unchanged keys, fresh gathered storage, and all six exercised
scalar gather types. See the [living findings](../../matrix/docs/next-version-findings.md)
for remaining sorting and native collection work.

## Numeric value ordering

`sort_i32`, `sort_i32_desc`, `sort_f64`, and `sort_f64_desc` now use the
Matrix stable merge implementation. They still mutate the supplied array
and return that same array, including empty and singleton inputs. Existing
aliases observe the sorted contents. The implementation first computes the
index order and copies the ordered values to temporary storage before
writing back, taking O(n log n) time and O(n) auxiliary storage with bounded
call depth. Repeated equal values no longer trigger quadratic quicksort
partitioning. Descending sorts preserve ties instead of reversing them.

The floating-point library uses this ordering contract:

| API | Behavior |
| --- | --- |
| `compare_f64(a, b)` | Numbers use numeric order; NaN is greater than every number; two NaNs compare equal. Signed zeros compare equal. |
| `sort_f64`, `sort_f64_desc` | Stable ascending or descending numeric order, with NaNs last in both directions. |
| `insertion_sort_f64`, `is_sorted_f64` | Use ascending order with NaNs last. The sortedness check leaves its input unchanged. |
| `binary_search_f64` | Requires ascending NaN-last input; returns any index with an equivalent key, or `-1`. A NaN target can match a NaN, and either zero can match either signed zero. |
| `min_f64`, `max_f64` | Use the ascending comparator and retain the first value on ties. Minimum chooses a number when any exists; maximum returns NaN when any exists. All-NaN input returns the first NaN; empty input returns `None`. |

This changes the older library behavior around NaNs. It does not change
the language's IEEE operators: NaN remains unequal to itself under `==`,
and ordinary ordered comparisons with NaN remain false. `sum_f64` and
`average_f64` continue to propagate NaN through ordinary arithmetic.
For finite inputs, `average_f64` uses compensated subtotals to avoid sum
overflow; see the [averaging contract](floating-averages.md) for accuracy limits.
Sorting preserves the original relative order of signed zeros and NaNs.

The explicitly named insertion sorts remain O(n²). `merge_sort_i32` retains
its separate recursive implementation and storage behavior, and `reverse_i32`
and `reverse_f64` still reverse all elements. Text value sorting uses the same
stable merge implementation, with the contract below.

Paired Ny and Matrix consumer fixtures exercise aliases, boundary sizes,
duplicate keys, infinities, signed zeros, NaNs, search, and extrema on the VM
and native routes. A separate VM metrics regression checks comparison growth
and call depth for equal, duplicate-heavy, sorted, and reversed inputs.

## Text ordering

Text `<`, `<=`, `>`, and `>=` compare content lexicographically by unsigned byte
value. At the first differing byte, the smaller byte sorts first; a proper
prefix sorts before the longer string. Empty text therefore sorts first in
ascending order. Case, accents, and Unicode normalization are not folded:
`"A" < "a"`, and distinct UTF-8 byte sequences remain distinct keys. There is
no locale-dependent collation. The native backend uses this same ordering for
its existing NUL-free text domain; embedded NUL remains unsupported natively.

`sort_text` and `sort_text_desc` now use the Matrix stable merge implementation
and return the original array after updating its contents. All aliases observe
the result, including subsequent writes and growth through the returned array.
Equal content retains its original order in either direction.
`stable_sort_indices_text` provides the corresponding original indices while
leaving the keys unchanged; its equal-key ordering is directly observable when
reordering parallel arrays with `try_gather`.

These sorts require O(n log n) key comparisons and O(n) temporary array storage
with bounded call depth. Comparing two strings may inspect their common prefix,
so this is not a constant-cost comparison guarantee. Repeated identical strings
no longer cause quadratic quicksort partitioning. The named
`insertion_sort_text` remains quadratic and stable; `reverse_text` still reverses
every element, including ties.

`compare_text`, `is_sorted_text`, `binary_search_text`, and `unique_text` retain
their content-based semantics. Binary search requires ascending input and may
return any matching index, or `-1` when absent. `unique_text` removes consecutive
equal entries; it does not sort its input. Empty and singleton value sorts keep
the original handle, while index sorting returns a fresh index array.

The paired text consumers cover both frontends, aliases, duplicate keys,
stable index permutations, UTF-8, control bytes, and dynamically constructed
strings. A separate comparison fixture exercises all ordered operators; the
VM metrics regression includes repeated strings and long common prefixes.

The whitespace helpers recognize space, tab, newline, carriage return,
form feed, and vertical tab. Ny's `\f` and `\v` escapes now decode correctly
through textual and binary NIL, so `is_whitespace("f")` and
`is_whitespace("v")` are false and `split_whitespace("five velvet")` returns
the two intact words. Paired Ny/Matrix consumers check actual control bytes
independently of their Ny escape spellings. Matrix retains its raw-string rules.

## Compiler and installation behavior

The CMake install copies active `.ny`, `.matrix`, and `.mat` sources beneath the SDK's `nilstd`
directory. `nilcompiler` discovers `nilstd` beside the installed root commands
and also accepts `NIL_STDLIB_PATH`, `-I`, `--sdk`, or `--sdk-path` to select a
source or SDK search root. `--no-stdlib` suppresses discovery. Library imports
remain explicit while the project does not have a stable implicit prelude.

The complete SDK builder compiles every installed source with normal type
checking into the mirrored `nillib` tree. It does not use `--no-typecheck` and
does not publish a partial SDK: a single rejected module aborts packaging and
leaves any previously validated SDK active. The compiler resolves imports from
either `nilstd` or `nillib`; the SDK's validation smoke deliberately exposes only
`nillib` to prove the binary distribution is independently consumable.

Each NIL library contains compiled NIL bodies and an embedded, checked Ny or Matrix
source interface. That interface keeps generic instantiation and semantic
analysis available without installing `nilstd`. The current bootstrap linker
re-checks the interface and regenerates imported bodies rather than linking the
stored bodies directly. This is a real binary-only distribution boundary, but
not yet a zero-recompilation module cache. Ordinary modules use v2; modules
with class declarations use v4 with explicit ancestry and method metadata.

The source extension selects the frontend, including for embedded interfaces.
Both languages can contribute pages to the same logical module and import the
same resulting NIL library. Matrix `book`/`library` declarations group sources
like Ny `module`/`package`. Matrix hyphenated exports become underscore names
in NIL and Ny: `try-copy-range` is `try_copy_range`. Keep the declared case for
imported names; cross-page case-insensitive name lookup is not yet provided.

For compiler smoke tests, use `--no-stdlib`: basic functions such as `println`
are currently provided by compiler/runtime intrinsics and do not demonstrate
that the broader source library is ready.

At the varint/CRC SDK checkpoint, the aggregate `prelude` and all 14 `nil.std`
module groups compile into checked NIL v2 libraries without `--no-typecheck`. Canonical
`Option`/`Result` construction, matching, selected methods, nested values,
iterators, successful unwraps, and invalid-unwrap failure are also exercised
through textual and binary `nilinterpreter`. This does not complete the whole
error model: postfix `?` remains an explicit compile-time rejection, and
unexercised higher-order operations are not implied by the passing subset.

The `nil.std.core` `assert_*` functions are executable library functions: the
route fixtures cover seven successful assertion forms and an exact panic for a
false assertion. They are distinct from the language-level `assert.*` intrinsic
syntax, which remains unavailable and fails closed.

## Native shims

`nilstd/native` preserves 14 platform implementations for selected I/O,
network, security, process, and threading APIs. These files are not a single
portable library target today. In particular, the threading shims are not
built or installed by the SDK; the active package therefore does not expose
their yield or processor-count operations. A Ny wrapper is not considered
supported merely because a matching C or C++ file exists. This preserved shim
tree is distinct from the versioned `nilruntime` archive used by the verified
native slices above.

## Experimental quarantine

Sources in `nilstd/experimental` were retained because they express
useful design work, but they lack one or more promotion requirements: coherent
language semantics, clean dependency resolution and type checking, a shipped
runtime provider, or a focused behavior contract. They are excluded from both
`nilstd` and `nillib`.

Promotion back to `src` requires all of the following:

1. the file parses without recovery diagnostics;
2. imports resolve from the canonical nested library root;
3. it type-checks without bypass flags;
4. required native code is connected through an explicit build target; and
5. a focused test runs through a supported test path.

See the component-level [nilstd README](../../nilstd/README.md) for the
latest cleanup audit. Until those criteria are met, examples should avoid
presenting library APIs as guaranteed behavior.

## Compiler tools and databases

The [analysis/reflection API](../analysis-api.md) provides compiler diagnostics
and immutable declaration snapshots for Ny or Matrix analyzers.
The [database API](database.md) adds shared SQLite and PostgreSQL execution
providers to `nil.std.db`, with explicit capability checks and bound parameters.
