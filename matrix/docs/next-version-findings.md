# Matrix and NIL next-version findings

Started **2026-10-15**. This is a living record from building real standard
library code in Matrix and consuming the shared NIL APIs from both Matrix and
Ny. Update an entry when its reproducer, scope, or verification changes; retain
the evidence when a finding is closed.

**Fixed** means the cited implementation and focused checks passed. It does
not imply every library or execution route is certified. **In progress** means
implementation or verification is unfinished. **Open limitation** describes a
confirmed missing capability. **Design friction** and **Observation** record
usability costs without treating an intentional language rule as a compiler bug.

## SDK and cross-language behavior

### F01 — Matrix standard-library sources were omitted from SDK packaging

**Status:** Fixed in the current development worktree.

**Evidence:** The compiler already resolved `.matrix`/`.mat` imports and
retained source suffixes in embedded NIL library sources. Installation,
packaging inventory, module grouping, and SDK validation accepted only `.ny`.
Those paths now accept all three suffixes. Matrix `book`/`library` grouping
preserves declaration case, maps hyphens to underscores, and excludes comments
and raw text. See [the source helper](../../scripts/sdk_sources.py),
[packager](../../scripts/package-sdk.py), and
[validator](../../scripts/validate-sdk.py).

The [focused SDK tests](../../tests/sdk_matrix_sources_tests.py) passed **9
tests**, including **16 consumer execution combinations**: two packaged
fixtures × source-only/binary-only imports × Ny/Matrix consumers × VM/native.
The fixtures cover mixed-language and Matrix-only modules, `.matrix` and
`.mat`, manifest completeness, and rejection of colliding artifact aliases.
Existing SDK artifact validation also passed. No NIL wire-format change or
generated Ny interface stub is required.

**Next step / acceptance:** Keep these checks in CTest. On each SDK publication,
confirm the real `array_checked.matrix` source and its logical NIL library are
present and usable. Preserve the separate historical Validation SDK.

### F02 — Imported names do not receive the same folding as local names

**Status:** Confirmed interoperability boundary; design decision remains open
for richer Matrix import resolution.

**Evidence:** [Matrix name resolution](../../nilcompiler/src/matrix_parser.cpp)
folds references against declarations collected from the current page. An
imported or qualified name retains its written case, with hyphens lowered to
underscores. A direct compiler probe on 2026-10-15 exported `Fetch-Value` from
`book demo.api`: a Matrix consumer calling `Fetch-Value with nothing` compiled;
`fetch-value with nothing` failed with `Undefined identifier 'fetch_value'`.
This also affects Matrix-to-Matrix imports, not only Ny imports. The NIL symbol
must retain its declaration's case for Ny interoperability.

**Next step / acceptance:** Add permanent cross-page tests for exact case,
hyphen/underscore spelling, and ambiguous folded names. If import-aware folding
is added, resolve to the original exported symbol without changing NIL identity.
Until then, retain the exported case; lowercase hyphenated APIs map naturally
to lowercase snake_case Ny calls.

### F03 — Import discovery probes lowercase source extensions

**Status:** Open limitation.

**Evidence:** The compiler dispatches direct `.MATRIX`/`.MAT` inputs correctly,
and packaging accepts them, but [source import lookup](../../nilcompiler/src/main.cpp)
constructs candidates ending in lowercase `.ny`, `.matrix`, and `.mat`.
On the case-sensitive development filesystem, renaming `demo/api.matrix` to
`demo/api.MATRIX` caused its previously compiling consumer to fail with an
undefined imported symbol. The probe was repeated on 2026-10-15.

**Next step / acceptance:** Decide whether canonical lowercase filenames are
the supported contract or import lookup should discover case variants. Test
both suffixes and ambiguous files before broadening lookup. Keep standard
library filenames lowercase meanwhile.

## Collections and native execution

### F04 — Raw array indexing is lenient; callers need explicit checked APIs

**Status:** Existing raw-indexing contract; checked-library expansion fixed and
verified for the six exercised scalar types.

**Evidence:** [The array bounds fixture](../../tests/language/array-bounds-consistency.ny)
and its expected output preserve zero/default reads and ignored writes outside
the array for the exercised scalar cases. That can conceal a caller's bounds
error when a result does not distinguish failure from a legitimate zero.

[The new Matrix library](../../nilstd/src/collections/array_checked.matrix)
adds `try_get`, `try_set`, `try_fill_range`, and `try_copy_range`, spelled with
hyphens in Matrix. Invalid arguments return `None` or `false` before mutation;
empty endpoint windows succeed. Subtraction-based bounds checks avoid signed
addition overflow, and copy handles overlap in either direction. These APIs
do not change the raw-indexing contract or grow storage.

**Verification:** The paired
[Ny](../../tests/language/checked-array-runtime.ny) and
[Matrix](../../tests/language/checked-array-matrix-runtime.matrix) consumers on
textual/binary NIL, both bundle forms, LLVM JIT, and LLVM AOT passed all **12
route slots**. They cover all six scalar types, extreme offsets/counts, empty
windows, unequal array lengths, shared aliases, both overlap directions, and
no mutation on failure. Floating native coverage now passes after F05.

**Next step / acceptance:** Preserve these route checks and document any new
aggregate element support separately. Expansion must retain full-window
validation before writes and exact overlap behavior.

### F05 — Native `Option<f64>` blocked checked floating-array reads

**Status:** Fixed; focused native and cross-route checks passed.

**Evidence:** The checked-array consumers reached a pre-existing native
rejection for a floating payload in `Option<T>`. A minimal Ny reproducer is
`let sample: Option<f64> = Option.Some(0.5)` after importing `ny.lang.core`.
The other exercised scalar specializations passed individual AOT probes.
This is a native enum-payload capability gap, not a Matrix parsing error.

**Verification:** The expanded
[generic-enum fixture](../../tests/language/native-generic-enum-construction.ny)
passed all six routes, covering floating construction/matching, payload
extraction, function parameters/returns, generic `unwrap`, negative fractions,
large values, signed zero distinguished with `atan2`, and NaN from `sqrt(-1)`.
Both complete checked-array consumers also passed all six routes. Five new
handwritten NIL rejection cases passed in the
[native NIL validation suite](../../tests/tools/native_nil_validation.py),
and the standalone enum assembler/linker pipeline passed.

**Next step / acceptance:** Retain both valid floating-payload behavior and
strict rejection of incompatible metadata. Do not generalize this result to
arbitrary `Option<userstruct>` or every enum ABI shape; F13 tracks the broader
diagnostic-quality issue separately.

### F06 — Collection `copy_to` offset arithmetic could overflow

**Status:** Fixed; focused copy/bounds checks passed.

**Evidence:** A baseline copy from HEAD reproduced `List.copy_to` accepting an
`INT_MAX` offset instead of rejecting it. Guards in List, Queue, Stack, HashSet,
and Dictionary now check remaining capacity using subtraction.
[The focused harness](../../tests/tools/collection_copy_bounds.py) checks
19 cases: eight successful empty/start/middle/end/full copies and eleven
rejected negative/minimum/outside/undersized/overflow cases. List, Queue, and
Stack run on VM/JIT/AOT; HashSet and Dictionary run on VM: **19 × 11 = 209
checks**. The harness checks panic text, exit status, copied values/order, and
surrounding sentinels.

**Next step / acceptance:** Keep `ny.collection-copy-bounds` green and extend
the HashSet/Dictionary route matrix when F07 is resolved. These 209 checks are
a specific copy/bounds proof, not blanket collection certification.

### F07 — Aggregate and nested array layouts limit native collections

**Status:** Open native-backend limitation.

**Evidence:** Native HashSet compilation rejects the `items` field of
`vec<map_entry<i32,bool>>` because `array<map_entry<i32,bool>>` lacks a supported
native handle layout. Dictionary also relies on map-entry storage and
`array<key_value_pair<i32,i32>>` destinations; its native compile likewise
rejects the internal `array<map_entry<i32,i32>>` field before executable
emission. A direct probe of `let rows: array<array<i32>> = [[1, 2]]` compiled to
NIL but native compilation rejected `NEW_ARRAY_LITERAL` with
`pointer-valued array elements are not implemented`. The copy harness therefore keeps
HashSet/Dictionary coverage on the VM; passing scalar List/Queue/Stack cases
does not establish record-array, nested-array, or complete collection support.

**Next step / acceptance:** Define supported aggregate/nested element layouts
and ownership behavior, add direct array storage/copy/lifetime tests, then run
HashSet and Dictionary copy and mutation fixtures through JIT/AOT. Keep unsupported
shapes diagnosed explicitly until they execute correctly.

## Editor and authoring experience

### F08 — Debugger names expose lowered Matrix spelling

**Status:** Open debugger limitation.

**Evidence:** Matrix source breakpoints work, but Locals displays `local_count`
for a source binding named `local-count`; Watch requires `local_count`, and the
original hyphenated spelling does not resolve. This is recorded in the
[extension debugger limitations](../../vscode/README.md#source-debugging).
Compiler source maps and debugger expression-name mapping are separate issues.

**Next step / acceptance:** Preserve source spelling in debug metadata and
resolve watches against the paused lexical scope. Test hyphens, underscores,
ASCII case variants, and inner shadowing without changing NIL symbol identity
or breaking Ny watches.

### F09 — Completion is document-local and lacks call guidance

**Status:** Open language-server limitation.

**Evidence:** Matrix completion, hover, definitions, and outline now use a
tolerant local declaration index. Imported books/STL symbols, whole-project
inference, signature help, and named-argument assistance are not implemented.
See [the current LSP scope](../../vscode/README.md#current-language-server-limits)
and [the protocol tests](../../tests/lsp_matrix_protocol.py), including the
unknown-import receiver case. Local completion success does not prove imported
generic APIs such as `try-get` can be discovered or explained by the editor.

**Next step / acceptance:** Index SDK and workspace exports, resolve imported
symbols to their source or library metadata, and offer parameter names/order
from the actual declaration. Exercise Ny-authored and Matrix-authored generic
APIs, aliases, changed documents, and ambiguous imports; provide signature
help during an incomplete named call.

### F10 — Long prose signatures and calls need formatting support

**Status:** Authoring observation; no syntax change proposed yet.

**Evidence:** `try-copy-range` in
[the checked-array source](../../nilstd/src/collections/array_checked.matrix)
has five named parameters and a long declaration line. Bounds expressions and
fully named calls likewise become wide. The LSP currently has no formatter;
existing multiline highlighting/indexing tests establish only those editing
behaviors, not canonical layout or safe format-on-save.
Multiline named calls already compile: the wrapped checked-copy example in
[the library reference](../../docs/standard-library/index.md#checked-arrays)
was run successfully with output `1` and `3`. This is formatting ergonomics,
not an inability of the parser to accept wrapped calls.

**Next step / acceptance:** Collect representative long signatures, calls,
generic types, and conditions. Choose wrapping/indentation rules that preserve
comments, raw text, and declaration spelling; require formatting to be
idempotent and compiled behavior to remain unchanged.

### F11 — Reserved words constrain natural parameter and field names

**Status:** Confirmed design friction.

**Evidence:** A 2026-10-15 compiler probe of
`example receives value as number returns number begin paragraph` failed at
`value` with `expected a parameter name, found 'value'`. Capitalization does
not avoid a keyword because keyword matching folds ASCII case. The current
library uses names such as `item-value`; backtick identifiers are another
supported escape. The [naming design](../README.md#the-naming-tax-measured)
already records this tradeoff.

The text-ordering consumer adds a cross-language example: Ny's
`new_with_value<T>(size, value)` is callable from Matrix, but its named
argument must be written `` `value` being ... ``. Plain `value being ...`
produces MX0101 and cascading MX0101/MX0104 diagnostics. The consumer uses the
documented backtick escape; this is API naming friction rather than a failure
of the shared generic implementation.

The RNG consumer adds `seed(value)`: Matrix can call the Ny function with
`` seed with `value` being 1 `` or positionally. Bare `value being 1` collides
with the keyword. Local names `reference` and `shared` also collided while
writing these tests. The paired RNG fixture keeps the escaped named argument
as an executable cross-language example.

**Next step / acceptance:** Keep the structural keyword rule explicit in
documentation and completion. Improve collision diagnostics/suggestions where
needed, and test useful alternatives such as `item-value` and escaped names.
Gather repeated real API examples before proposing contextual keywords or
other language changes.

### F12 — Matrix editor integration, highlighting, and source maps

**Status:** Fixed in the preceding editor integration pass; regression baseline.

**Evidence:** `.matrix` and `.mat` now participate in the language server,
compiler/native/interpreter tasks, Problems, and F5. Matrix declarations retain
source metadata for debugging. **Nine**
[TextMate/Oniguruma checks](../../vscode/src/test/matrixGrammar.test.ts)
cover case, complete hyphenated names, declarations/parameters, raw text,
multiline recovery, indentation, and snippets. **Fifteen**
[Matrix LSP protocol checks](../../tests/lsp_matrix_protocol.py) cover local
completion/navigation and tolerant editing. The preceding pass also passed
**13 real VS Code host checks** for both suffixes, saved/untitled documents,
commands, diagnostics, and debugger entry/cleanup using
[the host suite](../../vscode/test/suite.cjs).

**Next step / acceptance:** Preserve these baselines while adding imported
completion, formatting, and original debugger names. Add a focused token or
protocol case for each newly observed display problem; prior passing examples
do not establish that every grammar context or theme is correct.

### F13 — Unsupported native layouts can produce misleading type diagnostics

**Status:** Open diagnostic-quality follow-up, separate from F05's native fix.

**Evidence:** Before the floating enum-payload patch, native rejection reported
`requires exact type 'f64', got 'f64'`. The frontend types agreed; the backend
could not represent the requested payload. Repeating identical expected and
actual types made the unsupported-layout cause difficult to identify.

**Next step / acceptance:** Distinguish type mismatch from unsupported backend
representation. Add a negative fixture for a still-unsupported layout and
assert that its diagnostic identifies the capability and affected type, while
real type mismatches continue to report their differing types.

### F14 — Compiler usage text advertises only Ny input files

**Status:** Open discoverability issue; low priority.

**Evidence:** `nilcompiler --help` still prints
`<input1.ny> [<input2.ny> ...]` in its usage examples even though the driver
accepts `.ny`, `.matrix`, and `.mat` together. The
[driver help text](../../nilcompiler/src/main.cpp) confirms the omission.

**Next step / acceptance:** Show language-neutral input placeholders and list
the supported source suffixes. Verify the displayed examples work for both
frontends without suggesting a separate Matrix compiler is required.

### F15 — An imported language changed the caller's diagnostic rendering

**Status:** Fixed for explicit-input renderer selection; regression checks
passed. Per-diagnostic source provenance remains open.

**Evidence:** The full CTest run after adding the Matrix standard-library page
found two Ny rejection fixtures whose diagnostics changed to Matrix style and
type vocabulary. They still rejected the same invalid programs, returned an
error, and emitted no artifact. The
[driver](../../nilcompiler/src/main.cpp) previously selected Matrix rendering
when *any* parsed unit used Matrix, including transitive standard-library
dependencies. A Ny caller importing a newly Matrix-authored implementation
therefore changed diagnostic language without changing its own source.

The current patch selects this fallback renderer from explicit primary inputs.
That isolates ordinary Ny callers from a dependency's source language, but it
does not determine which page actually produced each error when several pages
are checked together. The shared checker diagnostic carries a line number
without a file or column. Matrix rendering can consequently show `<input>`
instead of a navigable source location; a build with mixed explicit inputs
still has only one global renderer choice.

**Verification:** Four cases in the
[Matrix execution suite](../../tests/matrix/run_execution_tests.py) cover Ny
and Matrix callers with opposite-language dependencies through source-only and
binary-only imports. They check exact diagnostic vocabulary, failure exit,
empty stdout, and preservation of an existing output artifact. Both original
Ny rejection fixtures pass unchanged and confirm no new artifact is emitted.

**Next step / acceptance:** Carry file identity and start/end positions through
the shared AST/checker diagnostic path, as outlined in
[the diagnostic design](diagnostics-and-analyzers.md). Exercise errors in both
callers and imported pages, including mixed explicit inputs; render each error
using its originating language and valid source span without guessing a file.
This attribution work is separate from F13's unsupported-layout wording.

## Stable ordering of parallel data

### F16 — Value sorting does not preserve a shared order across 3D attributes

**Status:** Implemented and verified for integer/floating index ordering and
the six exercised scalar gather element types.

**Evidence:** The existing
[sorting library](../../nilstd/src/core/sorting.ny) sorts or returns
scalar values. It does not return their original positions. Sorting a depth
array alone therefore cannot reorder parallel position, object-ID, color, or
other attribute arrays while preserving the association between their rows.
Stable value sorting by itself does not supply this missing index mapping.
For example, keys `[5, 2, 5]` need ascending indices `[1, 0, 2]`; the two equal
keys retain their original relative order. Descending order should be
`[0, 2, 1]`, preserving that same tie order rather than reversing it.

The focused addition is `stable_sort_indices_i32(keys, descending)` and
`stable_sort_indices_f64(keys, descending)` in the Matrix
[index-ordering module](../../nilstd/src/core/sorting_indices.matrix)
under `ny.lang.core`, returning fresh
`array<i32>` index permutations without changing the keys. Equal keys are
stable; positive and negative zero compare equally. Floating NaNs sort last
in both directions, retaining their original relative order. This explicit
policy initially belonged to the new API; F17 separately tracks its deliberate
adoption by the existing numeric value-ordering APIs.

`try_gather<T>(items, indices) -> Option<array<T>>` in
[the checked-array module](../../nilstd/src/collections/array_checked.matrix)
applies an index order to any supported element type. Duplicates and subsets
are valid. Any invalid index yields `None`; the entire index array is checked
before result storage is created. Empty indices yield `Some` containing a
fresh empty array, including for empty input. Neither input is modified.
The result has fresh array storage with shallow element copies, not a promise
of recursive cloning for reference-containing elements.

**Verification:** An independent Python stable-sort oracle passed
48 ordering cases on all six execution routes. It covered lengths
`0, 1, 2, 3, 7, 16, 17, 31, 32, 33, 127, 257`, both directions, duplicate
integer extrema, and floating NaNs, infinities, signed zeros, and fractions.
The local scratch report is `build/ordering-work/oracle-report.json`; this is
development evidence in addition to the permanent fixtures. No new frontend or
ABI blocker arose in that implementation.

The permanent [Ny consumer](../../tests/language/stable-index-ordering.ny) and
[Matrix consumer](../../tests/language/stable-index-ordering-matrix.matrix)
passed all **12 route slots**: textual/binary NIL, both bundle forms, LLVM JIT,
and LLVM AOT for each language, with no skips or expected failures. They check
ascending/descending permutations, tie stability, signed zero, infinities,
NaNs, empty/singleton arrays, and unchanged input storage. Gather checks cover
all six scalar types, duplicates/subsets, invalid negative/extreme/one-past
indices, empty gathers, aliased inputs, and independent result storage,
including `Option<array<f64>>` and `Option<array<text>>`. Literal depth, draw-ID,
and material expectations verify that one stable order keeps parallel scene
attributes aligned, with NaNs last.

**Next step / acceptance:** Retain these route checks and repeat source-only
and binary-only SDK consumption on publication of the real Matrix library.
Keep this proof scoped to these APIs rather than claiming aggregate-array
support or general comparator-based sorting. F17 records the separate numeric
value-ordering policy change and its verification.

### F17 — Numeric floating ordering lacked a consistent NaN policy

**Status:** Implemented and source-verified for the numeric APIs described
below. The historical reproducer is retained as evidence of the original
behavior; installed-SDK checks are a separate verification scope.

**Evidence:** A 2026-10-15 temporary consumer created a NaN with the `math.sqrt`
intrinsic applied to `-1.0` and used the then-existing
[comparison](../../nilstd/src/core/ordering.ny) and
[sorting](../../nilstd/src/core/sorting.ny) functions. The same
results were reproduced on the VM, LLVM JIT, and LLVM AOT before the numeric
value-ordering change:

| Operation | Observed result |
| --- | --- |
| `compare_f64(NaN, 1.0)` | `ordering.equal` |
| `compare_f64(1.0, NaN)` | `ordering.equal` |
| `compare_f64(NaN, NaN)` | `ordering.equal` |
| `sort_f64([2.0, NaN, 1.0])` | `[2.0, NaN, 1.0]` |
| `is_sorted_f64` on that result | `true` |

Both less-than and greater-than comparisons are false for an unordered NaN
operand, so the former `compare_f64` fell through to equality. The small-array
insertion sort and adjacent-order check likewise did not establish an order
across the NaN. This was shared library policy, not evidence of a VM/native
discrepancy or incorrect IEEE floating comparison in the compiler.

**Current resolution:** Retain the public names and adopt an explicit numeric
library order. `compare_f64` places NaNs above numbers, treats two NaNs as equal,
and treats positive and negative zero as equal. `sort_f64` and `sort_f64_desc`
use stable index ordering with NaNs last in both directions; ties retain their
original order. Descending NaN-last order therefore is not a simple reversal
of the ascending result. Raw IEEE operators retain their existing behavior.

`insertion_sort_f64`, `is_sorted_f64`, and `binary_search_f64` share the
ascending NaN-last policy. `min_f64` selects a numeric minimum when any number
is present and otherwise the first NaN; `max_f64` selects the first NaN if any
is present. Ties preserve the first encountered value, including its zero
sign. Empty reductions keep their `None` contract. `sum_f64` and `average_f64`
retain ordinary arithmetic propagation; this work does not filter NaNs out
of reductions.

**Verification:** An independent Python value-ordering oracle passed 48 cases
on all six routes, covering lengths through 1025, repeated equal values,
signed zeros, NaNs, infinities, and input/return alias behavior. The local
development report is `build/ordering-work/value-oracle-report.json`.

The permanent [Ny consumer](../../tests/language/numeric-value-ordering.ny) and
[Matrix consumer](../../tests/language/numeric-value-ordering-matrix.matrix)
passed all **12 source-route slots** across textual/binary NIL, both bundle
forms, LLVM JIT, and LLVM AOT. They cover both comparison argument orders,
NaN searches, orderedness, min/max
selection, mixed finite/NaN arrays, repeated NaNs, signed zeros, infinities,
and lengths around the former small-array threshold. They are registered in
the six-route CTest matrix and the
[installed SDK consumer suite](../../scripts/check-sdk-routes.py).

**Next step / acceptance:** Keep stable ascending/descending value order and
original-array/return aliases in the permanent route checks. On SDK
publication, repeat consumption through source-only and binary-only libraries;
source-route evidence does not by itself certify those installed artifacts.

### F18 — Numeric quicksort degenerated on large runs of equal keys

**Status:** Numeric-sort implementation and focused execution/comparison-growth
checks passed. The subsequent text-sort extension is recorded under F19.

**Historical evidence:** In the earlier
[sorting library](../../nilstd/src/core/sorting.ny),
`sort_i32` and `sort_f64` selected quicksort when the input length exceeded 16.
For an all-equal numeric input, every partition comparison `arr[j] <= pivot`
succeeded, placing the pivot at `high`. The recursive call therefore processed
`n - 1` elements, then `n - 2`, and so on. This gave quadratic comparison
work and linear recursive depth for that case, despite the median-of-three
pivot selection. This conclusion follows from the implementation; no timing
measurement or observed stack failure is claimed.

Repeated depths and repeated integer grouping keys are plausible 3D workloads,
so this is relevant beyond random-input sorting. F16's separate bottom-up
stable merge implementation avoids recursive partitioning; its permanent
fixtures already exercise 257 equal integer keys. That size smoke alone is
not a measured comparison bound for the value-sorting APIs.

**Current resolution:** Route `sort_i32`, `sort_f64`, and their descending
variants through Matrix stable index ordering. Gather the ordered values into
temporary storage before writing them back to the original array, preserving
both caller-visible mutation and the returned array's alias identity. The
intended bound is O(n log n) ordering work and O(n) auxiliary storage, with no
comparison callback API. The named insertion-sort entry points retain their
O(n²) algorithm; this is not a claim that every sorting function changed.

**Verification:** A test-only
[VM execution observer](../../tests/tools/vm_execution_metrics.cpp) counts
executed `CMP_*` instructions, total instructions, and peak VM call depth
through the ordinary stepping API. It does not rewrite or instrument library
source. The [numeric complexity harness](../../tests/tools/numeric_sort_complexity.py)
passed **32 profiles**: i32/f64 × ascending/descending × all-equal,
duplicate-heavy, sorted, and reverse-sorted patterns × lengths 256/512. Each
consumer also checks exact values, mutation, and returned/shared aliases.

Every current profile had maximum call depth **5**. Doubling the input grew
executed comparisons by **2.150–2.190×**, below the deliberately generous 3×
regression bound. Counts include linear input setup and verification; they
are not isolated sort-comparison counts or native timing measurements.

As an optional historical control, the harness restored `sorting.ny` from
revision `e30437391c1471cf923124d21ce5ee0799dd273d` in a temporary library copy.
For equal i32 keys, lengths 256/512 produced call depths **258/514** and
comparison counts **68,358/267,782** (about **3.917×** growth), violating both
guards. Current and historical consumers still passed their value/alias
checks. The local report is `build/numeric-ordering-work/complexity-report.json`.
The Git-history control is optional; the regular CTest has no Git dependency.

Direct-file imports of `ny.lang.core.sorting` also now declare their trait,
Option/Result, and Matrix helper dependencies, rather than relying on package
import order. Direct leaf-import consumers passed all **six checks** across
Ny/Matrix and VM/JIT/AOT.

**Next step / acceptance:** Retain the whole-program comparison-growth and
bounded-depth regression alongside the permanent six-route value checks.
The guard permits alternative bounded algorithms without depending on a
private comparator's name. Keep the observer out of installed tools, preserve
F17's floating policy, and report installed-SDK evidence separately.

### F19 — Text sorting inherited quadratic partitioning and lacked native ordering

**Status:** Stable text implementation and native ordered comparisons are
implemented and source-verified. Installed-SDK checks are a separate scope.

**Historical evidence:** [The text sorting functions](../../nilstd/src/core/sorting.ny)
selected `quicksort_text_range` for more than 16 elements. Its `<= pivot`
partition had the same all-equal `n - 1` recursive reduction described in F18,
implying quadratic key comparisons and linear recursive depth by inspection.
No historical elapsed-time or stack-failure observation is claimed.

A direct Ny probe of `"apple" < "banana"` succeeded on the VM but was rejected
by both LLVM JIT and AOT with `pointer-valued comparisons are not implemented
for CMP_LT`. This concerned ordered text comparison; equality already had
its own [fixture](../../tests/language/native-unsupported-text-comparison.ny).

**Current resolution:** `sort_text` and `sort_text_desc` use the shared Matrix
stable merge implementation, preserving the original array and returned
aliases. `stable_sort_indices_text` adds an unchanged-key permutation API,
including stable equal-content ties in either direction. The bound is
O(n log n) key comparisons and O(n) temporary array storage; each text
comparison also depends on string length. Explicit insertion sort, reverse,
search, sortedness, and consecutive-duplicate removal retain their contracts.

Native `<`, `<=`, `>`, and `>=` now compare text contents using the sign of
`strcmp`, matching the VM's unsigned-byte lexicographic order on NUL-free
text. There is no locale collation or Unicode normalization. Only two typed
text values enter this path; existing typed-pointer and scalar predicates
remain separate. Embedded NUL remains outside the native text contract.

**Verification:** The [comparison consumer](../../tests/language/text-comparison-runtime.ny)
passes all six routes, covering literal/parameter/returned/indexed values,
independent allocations, UTF-8, prefixes, supported control bytes, and empty
array reads. Strict native NIL validation passes 177 rejected cases and 20
accepted controls, including 96 new mixed-value or unsupported comparison
cases. Pointer, floating-point, existing text equality, escape parity, and
aggregate rejection regressions pass.

The VM [complexity regression](../../tests/tools/numeric_sort_complexity.py)
now passes 52 profiles, including 20 text cases for equal, duplicate-heavy,
sorted, reversed, and 256-byte common-prefix keys. All text cases have peak
call depth 5; doubling input from 256 to 512 grows whole-program comparison
instructions by at most 2.199×. These counts include setup and verification
and do not measure bytes inspected by string comparison or native timings.
The local report is `build/text-ordering-work/sort-complexity.json`.

The paired [Ny](../../tests/language/text-value-ordering.ny) and
[Matrix](../../tests/language/text-value-ordering-matrix.matrix) consumers pass
all 12 source-route slots. They cover independent expected values and stable
index permutations, 2048 equal strings, merge-boundary sizes, aliases and
growth, 448-byte common prefixes, and text retained across helper returns
and two rounds of 1600 × 1024-element integer-array allocation churn.

**Next step / acceptance:** Retain Ny/Matrix value and stable-index consumers,
including aliases and dynamically allocated text surviving allocation churn.
Repeat installed source/binary consumption on SDK publication. Locale-aware
collation and general comparison callbacks remain separate future APIs.

### F20 — Ny silently consumes unsupported escapes, corrupting whitespace helpers

**Status:** `\f`/`\v` source and NIL decoding repaired and source-verified;
diagnostics for other unsupported Ny escapes remain an open follow-up.

**Evidence:** Ny's source lexer decoded `\t`, `\n`, and `\r`, but discarded
the backslash from `\f`, `\v`, and `\q`, producing `f`, `v`, and `q` without
a diagnostic. The existing [whitespace helpers](../../nilstd/src/text/string_utils.ny)
use `\f` and `\v`, so a VM probe reported `is_whitespace("f")` and
`is_whitespace("v")` as true while rejecting actual form-feed and vertical-tab
bytes. `split_whitespace("five velvet")` produced `i`, `e`, `el`, `et`.
The old text fixture used the same unsupported spellings, masking the defect.
Ny VM and native JIT agree on the escape-decoding error; it is not an ordering
backend discrepancy. Scratch evidence is in
`build/text-ordering-work/friction-probes/report.json`.

Matrix quoted literals deliberately have
[no escape syntax](types-and-text.md#ordinary-text-has-no-escape-syntax); `\n` remains
two bytes there, while named `newline` and `tab` produce control characters.
That documented difference is not a Matrix decoder defect.

**Current resolution:** The [Ny lexer](../../nilcompiler/src/lexer.cpp)
now decodes `\f` and `\v`. The [NIL writer](../../nilcompiler/src/codegen.cpp)
escapes those bytes, and the [textual NIL reader](../../nilcompiler/src/nil_loader.cpp)
decodes them. Binary NIL already stores the raw string bytes. Matrix's lexer
and raw-string semantics are unchanged.

**Verification:** The paired [Ny](../../tests/language/text-whitespace-escapes.ny)
and [Matrix](../../tests/language/text-whitespace-escapes-matrix.matrix)
consumers pass all 12 source-route slots. Literal U+000B/U+000C bytes serve as
independent expectations; spelling both sides with escapes would repeat the
original blind spot. Tests cover `is_whitespace`, `is_blank`, trim variants,
`split_whitespace`, intact `five velvet` words, and literal backslashes that
must survive exactly one decoding pass. Matrix raw `\f`/`\v` literals remain
two-byte strings. Both consumers are registered in the installed SDK suite.
The older Matrix execution suite's Ny path example also expected lost
backslashes. Its Ny source now uses doubled backslashes and matches the
Matrix path value; the execution fixture and its explanation were corrected.

**Next step / acceptance:** Retain the independent byte oracles and installed
source/binary checks. Define the supported Ny escape set and diagnose unknown
escapes at their source location instead of silently changing literal content.

### F21 — Uninitialized Ny locals fail after source checking

**Status:** Open diagnostic-timing gap; observed for Ny text locals.

**Evidence:** `let blank: text` followed by `println(blank)` compiles to textual
NIL with an uninitialized `LOAD_LOCAL`. Execution reports `Local not found`,
while native compilation reports `Undefined local blank`. This is not an
implicit empty-text initializer. The minimal probe is retained at
`build/text-comparison-agent/default-local.ny`. Array default/out-of-range text
reads are a separate supported behavior and do return empty text.
Matrix requires an initializer and rejects the corresponding declaration with
`expected 'is' before the value, found 'say'`; the Ny probe does not establish
an equivalent Matrix runtime gap.

**Next step / acceptance:** Diagnose a local read before definite assignment
during source checking, with the declaration and failing read identified.
Preserve valid declaration-then-assignment code and test branches, loops,
and both frontends before making a broader definite-assignment guarantee.

## STL completeness audit — 2026-10-15

This inventory is a source review, not new runtime verification. Active SDK
sources, experimental sources, and opt-in packages are different scopes.
Existing VM fixtures and successful SDK packaging do not certify every API
on the native routes. The ordering below favors the 3D validation consumer.

| Priority | Gap | Evidence and proposed scope |
| --- | --- | --- |
| 1 | Native application-data storage | F07 remains the prerequisite for arrays of records and nested arrays. Native array element support also excludes `f32`. Vertex records and many generic collections need compiler/runtime work, not just new STL declarations. |
| 2 | Predictable buffers | [`vec.with_capacity`](../../nilstd/src/collections/vector.ny) ignores its argument; `capacity()` reports backing-array length. Implement actual reservation and resizing, fresh clone/subrange and append-range operations, then checked slices/views. A type-checker recognition of `resize` is not an implemented library contract. |
| 3 | Collections that scale | [`HashMap`](../../nilstd/src/collections/hash_map.ny) stores hashes but still scans entries. [`map`](../../nilstd/src/collections/map.ny) explicitly uses linear storage. [`queue.dequeue`](../../nilstd/src/collections/queue.ny) removes index zero and shifts the tail. Add bucketed hashing and a ring queue/deque with measured growth and collision coverage. |
| 4 | Strict serialization | [`NyData`](../../nilstd/src/nydata/parser.ny) can return partially parsed objects/lists at EOF, skip child errors, and return a first value without validating the remaining input. Add checked parsing, source locations, input/depth limits, and malformed-input fixtures. JSON is experimental; its current Unicode escape handling must be repaired before promotion. |
| 5 | Numerically robust math | F27 adds scaled norms, checked normalization, and finite-value helpers to [`vec2`/`vec3`](../../nilstd/src/math/vector.ny). Interpolation and relative-plus-absolute comparison remain gaps; existing `approx_eq` uses absolute tolerance. Dot/cross products and squared lengths still use ordinary arithmetic. |
| 6 | Reusable sequence algorithms | `vec.map`, `vec.filter`, and container-specific iterators exist. A common iterator contract, sequence zip/fold/chunks/windows, and comparator/key-based sorting remain gaps. Existing stable numeric/text sorts and checked copy/fill/gather should be reused. |
| 7 | Asset binary and text utilities | Extend checked integer binary helpers with floating-point encoding and cursor readers. Add an efficient text builder, formatting, and explicitly defined UTF-8 operations; current text utilities are predominantly byte/ASCII based. The experimental builder currently concatenates on append, so promotion alone would not provide amortized growth. |
| 8 | IO and independent state | Complete buffered/exact stream helpers, directory/metadata/temp-file operations and native IO parity. Add independent seeded RNG instances alongside the current global generator. Existing secure whole-file byte APIs already have narrow native evidence. |

The [engine math package](../../packages/ny.engine/src/math.ny) already
contains matrix, projection, and camera-transform code; its quaternion API
needs further work. Qualify and complete that package instead of adding a
second incompatible matrix implementation to the STL. General-purpose
vector correctness belongs in the shared math foundation.

Broader missing shipped capabilities include subprocess management,
network/TLS and database execution providers, general threads/channels/locks,
calendar/duration APIs, cryptography/CSPRNG, and compression. Some have
experimental declarations or implementations, which is not a supported SDK
contract. Language-level `parallel_map` already exists; HTTP/SQL builders,
Base64/hex, CRC, path helpers, and basic file APIs also already exist.

### Validation consumer evidence needs a refresh

The current `/Incubator/Execute/Validation/ny-3d-validation` source differs
from its recorded baseline. Its current scene/procedural sine tables each
contain 400 entries while indexing modulo 512, and scene hierarchy code adds
Euler angles instead of composing rotations. The scene-player oracle still
checks older translation/scale behavior. These are consumer source-review
findings, not reproduced STL/compiler regressions. Repair and independently
verify the current consumer before using it as evidence for new math work;
do not relabel historical baseline results as current-source results.

## Framework identity and layout checkpoint — 2026-10-15

The checkout is now `exec-nil-framework`. Component directories (`nilcompiler`,
`nilinterpreter`, `nildebugger`, `nilassembler`, `nillinker`, `nillsp`, `nilschema`,
`nilagent`, `nilaix`, `nilstd`, and `packages`) live directly under its root.
`ny` and `matrix` are language entry points; `vscode` contains the editor
extension. The `code` and `ext` wrapper directories are removed.

The SDK helper publishes to `<checkout>/sdk`, with source libraries in `nilstd`
and compiled modules in `nillib`. Linux runtime artifacts are `libnil.so` and
`libnil.a` under `nillib/native`; native AOT still selects the static companion.
The new `nil.sdk.v1` manifest seals both runtime forms and validates the shared
ABI in an isolated process. Build work remains under `build`, including retained
pre-move artifacts; old absolute-path CMake caches must not be reused.

Ny/Matrix language names, file extensions, `ny.lang.*` imports, NIL wire formats,
and `nyrt_*` ABI symbols remain unchanged. The VS Code extension retains its
installed identity and `ny.*` configuration keys; public tool discovery now
uses the NIL names. Canonical build/import settings use `NIL_*`, with supported
`NY_*` fallbacks when the canonical value is absent.

The remote Git repository has not been renamed. Renamed local release assets
identify `NIL Framework` and use `nil-sdk-*`; importer compatibility for those
assets requires separate verification before publishing them. Source, SDK,
consumer-route, and editor evidence for this change is retained under
`build/rebrand-work` and `build/vscode-host-smoke`.


## Reflection, analysis, and database checkpoint — 2026-10-15

Public imports now use `nil.std.*`, with `ny.lang.*` retained as aliases.
The namespace root is omitted from `nilstd/src`, `tests`, `native`, and
`experimental` folder paths. Installed source families are under `nilstd/core`,
`nilstd/reflection`, etc.; compiled families use `nillib/core.nil`, etc.

The first [analysis API](../../docs/analysis-api.md) runs shared parser and
semantic checks on current editor buffers without code generation or execution.
The VS Code client adds cancellable live checks and version-guarded fixes. The
first reflection library exposes declaration/annotation values and generated
snapshot modules; it does not yet expose semantic references or mutable ASTs.
SQLite and PostgreSQL now use a shared context-owned VM/native provider behind
`nil.std.db`. See the [provider contract](../../docs/standard-library/database.md).

### F22 — Semantic reflection needs references and control flow

**Status:** Open limitation, with declaration snapshot foundation implemented.

Compiler scopes and inferred types exist internally, but there is no stable
public use-to-declaration map, control-flow graph, or plugin callback host.
Guessing unused variables from identifier spelling would produce false results
under shadowing and imports. Keep default rules to real parser/type-checker
findings and verified formatting fixes until those semantic queries exist.
The standalone Matrix analyzer example proves language-authored inspection on
VM/native routes; it is an explicit program, not an automatically sandboxed plugin.

### F23 — Textual includes cannot provide trustworthy editor spans

**Status:** Open limitation; misleading locations withheld by analysis.

The Ny preprocessor flattens `@include` text without a source map. Function
ownership now supplies source paths for shared checker diagnostics, improving
F15, but included text can shift lines and change ownership. Analysis suppresses
spans and reflected line numbers for affected units. Older ownership/contract
checks without source provenance remain unlocated for multi-file compilations.
Next: propagate original file/byte spans through preprocessing, AST nodes, and
all checker contracts instead of inferring provenance from the final line number.

### F24 — Reflection formatting exposed missing native conversion intrinsics

**Status:** Formatter fixed; checked i64 parsing now has native support;
broader conversion parity remains open.

A pure Matrix diagnostic formatter using `Convert.char_to_i32`,
`Convert.i32_to_char`, and `Convert.to_hex_padded` compiled and ran in the VM but
failed native lowering. The shipped helper now uses portable text operations
and small Ny control-character constants. The paired Ny/Matrix fixtures exercise
UTF-8, quotes, backslashes, control escaping, and declaration values through all
six routes. Do not infer native support for every `Convert` intrinsic from this.

The v1 library pass also found that `Convert.try_to_i64` succeeded in both
interpreted frontends but failed LLVM lowering. Native support now constructs
the existing `Result<i64, text>` with the correct payload width and managed
error text. VM/native integer parsing shares one implementation; paired
fixtures and direct ABI tests pin signed extrema, invalid input and exact
errors. See the [checked integer-conversion contract](../../docs/standard-library/integer-conversion.md).

### F25 — Fully qualified enum patterns silently miss matching values

**Status:** Fixed for checked statement and expression patterns in Ny and Matrix.

**Original evidence:** With `import nil.std.core`, construct `Option.Some(9)` as
`Option<i32>`. A match arm written `nil::std::core::Option::Some(value)` did not
run; `Option.Some(value)` did. A following print executed and the program exited
zero. The legacy-qualified `ny::lang::core::Option::Some` spelling had the same
problem in shared enum-pattern lowering.
The minimal reproducer is retained in `build/std-namespace-work/qualified-match.ny`.

**Resolution:** The shared checker resolves pattern owners and retains their
declaration identity. Construction and matching now use the same canonical
runtime tag, while preserving the standard `Option`/`Result` tags required by
runtime providers. Matrix value-position `consider` patterns also accept
qualified paths and bind their payloads through the shared checker. Unknown
owners, missing variants, and patterns for the wrong target enum are rejected
during source checking, including `--analyze`.

**Verification:** Paired [Ny](../../tests/language/qualified-enum-patterns.ny)
and [Matrix](../../tests/language/qualified-enum-patterns-matrix.matrix)
consumers cover canonical and legacy namespaces, qualified construction,
statement and expression matches, guards, payload and unit variants, and user
enums through all six routes and both standalone assembler/linker pipelines.
The [rejection contract](../../tests/tools/qualified_enum_contract.py) checks
invalid owners/variants and target types in ordinary compilation and analysis.
Both positive consumers are registered for installed source/binary checks.

### F26 — Database results need richer values and resource ergonomics

**Status:** Initial text/NULL provider implemented; extensions planned.

The first shared provider binds text/NULL parameters, exposes checked result
access, bounds retained results, and closes resources with execution context
teardown. Future work includes typed numeric/date/binary bindings, explicit
statement objects, streaming, transaction helpers, connection pooling, and
schema/SQL-aware completion. PostgreSQL currently supports POSIX numeric-host
or private Unix-socket connections; SQLite and PostgreSQL capability depends on
available runtime client libraries. Provider errors do not echo credentials or
SQL values. Result-limit failure is not a transaction rollback guarantee.

### F27 — Vector normalization lost finite directions at extreme magnitudes

**Status:** Fixed for the paired Ny/Matrix source fixtures; public contract
documented in the [vector reference](../../docs/standard-library/math-vectors.md).

**Evidence:** `vec2.new(3 * scale, 4 * scale)` with `scale = pow(10.0, 200.0)`
returned infinite length and a zero normalized vector. At `pow(10.0, -200.0)`,
the squared length underflowed to zero, so `normalize()` returned the original
tiny vector. Both defects reproduced on all six execution routes. The old
math fixture only exercised ordinary vector magnitudes.

**Resolution:** Matrix-authored `hypot2`, `hypot3`, and `is_finite` helpers
provide shared scaled norms and scalar classification. Existing vector
`length()` methods use the new helpers. `vec2` and `vec3` add `is_finite()` and
`try_normalize() -> Option<vecN>`. Checked normalization divides each component
by the largest magnitude before computing the unit direction. It accepts
finite nonzero vectors even when their unscaled length overflows, including
subnormal inputs whose reciprocal would overflow. Zero, NaN and infinity
return `None`. `normalize()` preserves zero inputs and now consistently
preserves non-finite inputs; previously their results could mix zeros and NaNs.

**Verification:** [Ny](../../tests/language/vector-norms.ny) and
[Matrix](../../tests/language/vector-norms-matrix.matrix) consumers pass all
12 source-route slots. Independent coordinate expectations cover Pythagorean
directions, large/tiny normal values, the smallest subnormal, unrepresentable
lengths, signed zero, non-finite components, and preservation of input aliases.
Native `Option<vec2>` and `Option<vec3>` work for these fixtures; this is not a
blanket claim about arbitrary aggregate enum payloads. The fixtures are also
registered for installed source/binary consumption.

**Remaining limits:** These approximate norms are not correctly rounded.
Rounding near the upper finite boundary can produce infinity even for a
representable mathematical norm; checked normalization remains finite.
Squared lengths, dot/cross products and distance's initial subtraction can
still overflow. F29 removes the standard RNG's aggregate-global native blocker
and adds independent state; this does not add general native aggregate-global
support. Relative/absolute comparison and interpolation remain future work.
Probes and route evidence are under `build/math-library-work`.

### F28 — Scientific notation needs a source-language contract

**Status:** Implemented in both frontends and shared NIL decoding.

**Evidence:** The numeric literal `1.0e200` is not accepted by either Ny or
Matrix. Ny treats the exponent suffix as an undefined identifier; Matrix
reports `expected a statement, found 'e200'`. The vector fixtures therefore
construct extreme magnitudes with `pow`, including `pow(2.0, -1074.0)` for the
smallest subnormal. Minimal probes are in
`build/math-library-work/literal-probes`.

**Resolution:** Decimal literals accept `e`/`E`, an optional exponent sign,
and mandatory exponent digits. The exponent makes the value `f64`, even without
a decimal point. Hexadecimal `e`/`E` digits retain their integer meaning.
Shared locale-independent conversion accepts finite normal/subnormal values,
rejects overflow and nonzero values rounding to zero, and accepts exact zero
with arbitrarily large exponents. Validation runs before lowering and in editor
analysis, including numeric match patterns and unchecked compilation. The VM,
NIL validator and LLVM backend use the same conversion. Preprocessing preserves
exponent tokens without hiding neighboring range macros. Editor highlighting
and word selection include the complete signed exponent.

**Verification:** Paired [Ny](../../tests/language/scientific-literals.ny) and
[Matrix](../../tests/language/scientific-literals-matrix.matrix) fixtures cover
inferred types, globals, arrays, statement/expression matches, signed zero,
maximum finite values, normal/subnormal boundaries and ordinary/hexadecimal
literals through all six routes and both assembler/linker pipelines. The
[negative contract](../../tests/tools/scientific_literals_contract.py) checks
malformed/range diagnostics in both languages, editor analysis, all compilation
modes, unchecked builds, and preservation of an existing output artifact.
Parser, lexer, preprocessing and NIL-loader tests cover token boundaries and
direct textual/binary NIL. Textual NIL retains its existing `.5` and `1.`
mantissas; source syntax still requires digits on both sides of a decimal point.

### F29 — Random state blocked native code and range promises exceeded the implementation

**Status:** Fixed for the scoped random API; its distribution contract is
explicit in the [random-generator reference](../../docs/standard-library/random.md).

**Evidence:** The global RNG's aggregate state lacked native global metadata,
preventing the existing broad math fixture from compiling natively. Floating
ranges accepted NaN/infinite endpoints, overflowed when subtracting opposite
finite extremes, and could round to the excluded upper endpoint for adjacent
values. The integer sampler's comment incorrectly claimed unbiased full-i32
coverage: two consecutive MINSTD draws are determined by one of only
2147483646 states, fewer than the 2^32 possible i32 outputs.

**Resolution:** Scalar global storage preserves the original sequence and uses
the existing native global contract. Matrix-authored `random_generator` adds
constructor/reseed, scalar draws, bounded draws and explicit cloning. Every
step normalizes state before multiplication, including public-field edits.
Assignment shares state; cloning allocates independent state at the same
position. Floating bounds must be finite; equal finite bounds preserve their
minimum without advancing state. Overflowing spans use weighted endpoints,
and rounded results outside the half-open interval return the minimum.
Integer sequences and rejection behavior remain compatible, with unsupported
uniformity/full-domain claims removed.

**Verification:** Paired [Ny](../../tests/language/random-generators.ny) and
[Matrix](../../tests/language/random-generators-matrix.matrix) consumers check
independently computed modular-exponentiation sequences, extreme seed
normalization, aliasing/cloning, global/instance isolation, inclusive integer
bounds, a rejection requiring four draws, and finite/adjacent/subnormal floating
intervals. Negative cases cover both APIs and languages. The broad existing
`math-runtime` fixture now runs through all six routes with unchanged output.
The new fixtures and broad math consumer are registered for installed source
and binary-library imports, including assembler/linker pipelines.

**Remaining limits:** MINSTD is deterministic and non-cryptographic; separately
owned streams are not a statistical independence guarantee. Wider-state and
uniform full-width sampling remain future work. The global integer-range
wrapper uses a temporary record; reusable instances avoid that allocation.
General native aggregate globals remain unsupported. Audit oracles and route
evidence are under `build/random-library-work`.

### F30 — Large integer expressions can narrow before an i64 argument conversion

**Status:** Literal-range and mixed-integer-array fixes implemented; contextual
typing and Matrix's universal `number` default remain open.

**Evidence before the fix:** Passing `-9223372036854775807 - 1` to an `i64` parameter produces
zero on both frontends and all six routes. The emitted NIL performs `NEG i32`
and `SUB i32` before the call. The generator's normalization was correct; its
test had constructed the wrong seed. Oversized bare literals are accepted
without a width diagnostic, and the outer parameter type does not widen inner
arithmetic. A small-literal array also infers `array<i32>` in Matrix rather than
using an enclosing `array<i64>` annotation to widen its elements.

Independent Ny/Matrix probes also show `2147483647 + 1` (Matrix `plus`)
wrapping to `-2147483648` before conversion into an i64 destination/parameter.
Binding the first operand as i64 produces `2147483648` on every route. Matrix's
unannotated arithmetic therefore does not yet follow the intended wider
`number` default. Its type reference now states the actual distinction between
declared types and inferred literal expressions. The probe sources, NIL and
12 successful route executions are in `build/random-library-work/width-probes`.

**Resolution:** [NIL-0007](../../docs/design/NIL-0007-integer-literal-widths.md)
defines shared range-aware inference: decimal literals use `i32` when
representable and `i64` otherwise. Signed boundary literals represent both
minimum values directly. Hexadecimal masks through `0xFFFFFFFF` retain their
historical `i32` width; larger supported hexadecimal values use `i64`. Values
outside the signed 64-bit carrier range fail before artifact output, including
unused/default expressions, editor analysis and unchecked compilation.
Mixed `i32`/`i64` arrays now infer `array<i64>` in either element order,
including elements supplied by typed variables. Array types remain invariant.
The original large-literal expression now constructs the intended i64 minimum.
This repairs the existing `abs_i64` guard in `nil.std.core`: the old SDK
panicked for zero and returned the unchanged minimum value for overflow.
The baseline reproducer is under `build/integer-width-work/abs-baseline`.

**Verification:** Paired
[Ny](../../tests/language/integer-literal-widths.ny) and
[Matrix](../../tests/language/integer-literal-widths-matrix.matrix) fixtures cover
integer boundaries, globals, nested calls, both match forms, wide array values,
explicit narrowing, retained wrapping behavior and core `abs_i64` results.
Separate paired overflow fixtures require `abs_i64` to reject the signed
minimum on every enabled execution route. The
[contract runner](../../tests/tools/integer_literals_contract.py) checks invalid
source literals and existing-artifact preservation across compilation modes,
editor diagnostics, array invariance, and the type-checking-disabled flag.
Fixtures are registered for all six routes, standalone assembler/linker
pipelines and installed source/binary-library consumption.

**Remaining context / workaround:** To widen arithmetic on small operands,
bind one operand as `i64` in Ny or `number` in Matrix before the operation.
For example, `(2147483647 as i64) + 1` retains the wider result. The signed
minimum itself can now be written directly in either language.

**Next step / acceptance:** Decide broader contextual inference and Matrix's
intended universal `number` default separately. Small literals still infer
`i32`, so `2147483647 + 1` still wraps before assignment to `i64`; a parameter
or destination annotation does not change an inner expression's width. A
future change must cover generic identities, mixed-language arrays/calls and
explicit narrow arithmetic, with matching editor and execution evidence.

### F31 — Text substring bounds overflowed before clamping

**Status:** Compatibility fix and checked text-span APIs implemented.

**Evidence:** With the prior SDK, `substring("abcd", 1, 9223372036854775807)`
returns empty text rather than `"bcd"`. Adding start and length wraps before
the bounds check, then the narrowed count is negative. The underlying text
runtime already checks the remaining length safely; the library wrapper
introduced the error. Baseline evidence is in
`build/text-core-work/substring-baseline.json` in the v1 library checkout.

**Resolution:** Validate the start, then compare the requested length with
`size - start`. The existing helper retains its empty/clipped result policy.
Matrix-authored `try_substring` and `try_char_at` add exact checked byte access
as `Option<text>`, including `Some("")` for valid empty spans and `None` for
invalid bounds. They retain `i64` arguments until validation and share the
same public `nil.std.text` identity across frontends and legacy imports.
See the [text-span contract](../../docs/standard-library/text-spans.md).

**Verification:** Paired source fixtures cover whole/empty/end spans, invalid
and extreme positions/counts, UTF-8 byte positions, source preservation,
aliases, returned values and compatibility clamping. They are registered for
all execution routes, standalone assembler/linker pipelines and installed
source/binary libraries.

**Authoring friction:** A Matrix documentation example using
`when Option.Some holding value then say value` fails because `value` is a
reserved binding name, but the diagnostic says it expected `then`. Renaming
the binding to `span` works. A future diagnostic should identify the reserved
pattern binding directly; no grammar change was made in this pass.

### F32 — Floating averages can overflow for finite inputs

**Status:** Fixed for finite-input overflow; severe-cancellation accuracy remains limited.

`average_f64` previously computed `sum_f64(items) / count`. For
`[1e308, 1e308]`, the intermediate sum overflows to positive infinity although
the mean `1e308` is representable. Negative inputs behave symmetrically, and
`[1e308, 1e308, -1e308]` also produces positive infinity instead of a finite
mean. Paired Ny/Matrix probes reproduce these results on all six routes;
sources and reports are under `build/text-core-work/average-overflow` in the
v1 library checkout.

**Resolution:** Accumulate two compensated subtotals. Values of magnitude at
most one retain their scale so subnormal terms survive; larger values use an
exact power-of-two divisor greater than the count, leaving overflow headroom.
Retain the rounding tail when merging the subtotals, restore the original
scale when safe, and clamp the result to the input interval. `sum_f64` is
unchanged. Empty inputs still return `None`; any nonfinite input deliberately
uses the original ordered sum/division path, including its order dependence.

**Verification:** Paired Ny/Matrix fixtures cover finite extremes, repeated
maximum values, cancellation across both subtotals, subnormal ties, aliases
and nonfinite compatibility on all execution routes and installed libraries.
Finite expectations come from exact rational arithmetic on the binary64
inputs. The [averaging contract](../../docs/standard-library/floating-averages.md)
records the algorithm and evidence boundary.

**Remaining numerical limitation:** Compensation is not exact accumulation.
For `[1e100, 1e308, 1.7976931348623157e308, -1e308, -1.7976931348623157e308]`,
the small residual can still disappear, yielding zero instead of a mean near
`2e99`. Do not promise correct rounding, universal ULP accuracy or permutation
invariance. An exact or expanded accumulator is separate future work. There
is no new public API or compiler intrinsic in this fix.

### F33 — Native iteration over an array literal selects an absent iterator

**Status:** Confirmed open lowering bug.

Iterating directly over a text array literal prints its values in the VM, but
native compilation names an undefined `__ritual_iter` callee. Both Ny and
Matrix reproduce this on the prior SDK. Indexed traversal of a bound array
using `range`/`repeat while` works and is used by the checked-conversion
fixtures. Minimal programs and the VM/native comparison are preserved under
`build/convert-audit/foreach-literal` in the v1 library checkout.

**Next step:** Carry the iterable expression's resolved array type into loop
lowering, including literals and returned arrays, and qualify element widths,
text lifetimes and empty arrays across routes.

### F34 — Renamed module aliases survive into unresolved function calls

**Status:** Confirmed open name-resolution bug.

Ny `namespace words = ny.lang.text` followed by `words::substring(...)`
passes source compilation but emits a call containing the alias name rather
than the actual module owner. The VM reports an unknown function. Matrix's
`read nil.std.text as words` has the same lowering failure when combined with
an ordinary import that makes the function discoverable. These minimal cases
use the existing `substring`, so the checked-span addition did not cause the
failure. Prior-SDK sources and diagnostics are under
`build/text-core-work/friction-repros/namespace-alias.*` and
`baseline-results.json`.

Fully qualified canonical and legacy module names work; the checked-text
fixtures exercise those spellings. Namespace renaming is distinct from the
supported built-in `ny.lang.*` to `nil.std.*` compatibility mapping.

**Next step:** Resolve renamed import/namespace owners consistently during
checking and code generation, with mixed-language source and binary-library
tests.

### F35 — A Matrix generic parameter can consume the next parameter

**Status:** Confirmed open parser bug.

A signature containing `actual as Option of text, label as text` fails at
the second parameter's `as`, reporting `expected 'begin paragraph' to open
the body` and MX0105. Moving the generic parameter last allows the equivalent
program to compile and print `ok`. The prior SDK reproduces both outcomes;
minimal sources and exact diagnostics are in
`build/text-core-work/friction-repros/generic-parameter.matrix` and
`baseline-results.json`.

The checked-text fixtures put `Option of text` last as a temporary workaround.
**Next step:** Distinguish commas within generic arguments from commas between
function parameters, then test nested generic types and named arguments without
requiring parameter reordering.

### F36 — Conversion intrinsics do not preserve a custom Result owner

**Status:** Confirmed open provider-declaration validation gap.

A user-defined `custom::Result<T,E>` can be declared as the return type of a
function annotated `@intrinsic("convert.try_to_i32")`. The native ABI check
accepts its shape, but the provider constructs the standard `Result::Ok` tag;
matching the custom result misses. The VM has the same mismatch. The prior-SDK
probe returns its fallthrough sentinel `99` instead of parsed value `7` on
both routes. Evidence is under `build/text-core-work/review/provider-owner*`.

The existing public `nil.std.core.Convert` API uses the standard Result owner
and is unaffected. The new checked-i64 provider follows the same ownership
convention; this pass tests malformed structural signatures but does not add
custom enum support. **Next step:** Reject an incompatible nominal provider
owner during source/NIL validation, or explicitly support that owner throughout
construction and matching, with matching VM/native evidence.

### F37 — Editor and application builds lacked shared project context

**Status:** Initial project/check/build integration implemented; semantic
workspace services remain open work.

The extension previously offered file tasks and repository CMake/CTest tasks,
while compiler analysis supplied only two style rules beyond compiler errors.
No local application project driver or target manifest was implemented. The
language server's tolerant document index also remained separate from compiler
import/type resolution, and imported diagnostics appeared only in an output
channel.

**Resolution:** `nilbuild` and `nil.project.v1` establish one explicit local
project context for checking, VM/native builds and running. Builds check first
and publish staged outputs only after success. Compiler-owned default rules
add conservative unreachable-statement and discarded-standard-outcome warnings,
with a discoverable catalog, rule suppression and warnings-as-errors. VS Code
uses project context for live checks and project tasks, and publishes imported
Problems only when an exact bounded source snapshot matches current disk text
or a captured editor buffer (extended by F39).

**Remaining boundaries:** One executable entrypoint and its import graph;
complete rebuilds; Linux process execution. Unopened imports are type checked
but not recursively linted. The analyzer does not inspect lambda bodies inside
expressions or prove arbitrary control-flow reachability. Complete semantic
indexing (beyond the initial bindings in F40), semantic completion,
cross-file rename/references, language-authored analyzer hosting, project
reference graphs and incremental builds remain separate increments. See the
[developer-tooling plan](../../docs/developer-tooling.md) for acceptance criteria.

### F38 — The frontend directory could hide the repository standard library

**Status:** SDK discovery fixed; compatibility regressions added.

`nilcompiler --analyze -I <repository>` reported `NIL0003` for `nil.std.text`,
while adding `<repository>/nilstd/src` resolved the same import. SDK discovery
mistook the repository's `ny/` frontend directory for a legacy standard-library
root and stopped before considering `nilstd/src`. The editor's repository-root
configuration exposed this collision.

**Resolution:** Source and binary SDK discovery now recognize the historical
`ny/lang/` namespace tree instead of any directory named `ny/`. Canonical SDK
markers and search order remain intact. The
[analysis contract](../../tests/tools/analysis_contract.py) covers Ny and Matrix
imports through the repository root from an unrelated working directory, plus
a legacy `share/ny/stdlib/ny/lang` layout without a prelude marker. The new
repository-root regression reproduces the failure with the previous compiler.

### F39 — Unsaved dependencies were invisible to compiler-backed editor checks

**Status:** Workspace source overlays implemented; initial semantic bindings added in F40.

The original analysis command accepted one unsaved primary buffer. Imports and
Ny textual includes still came from disk, so editing a dependency could leave
the caller's checks inconsistent with its visible source. Simply hiding dirty
import diagnostics did not correct the program being analyzed. The editor also
guarded results primarily by the active document's version.

**Resolution:** A bounded `nil.analysis.workspace.v1` request supplies exact
source texts to the compiler's existing import and include resolution. New
source files can participate without being written to disk. Canonical path
identities prevent conflicting aliases. The editor captures open local source
buffers together, invalidates results on input changes and maps imported
diagnostics only against matching snapshots. Build tasks continue to use saved
files. See [NIL-0009](../../docs/design/NIL-0009-workspace-analysis.md).

**Boundary:** Overlays cover `.ny`, `.matrix` and `.mat`. Other textual include
extensions remain disk inputs. F40 adds exact watching for all native sources
in successful semantic snapshots and rejects navigation when an unsupported
include buffer is dirty.

**Follow-up:** F40 establishes original-source spans and retained binding IDs.
Complete references, transactional rename and semantic completion still need
the remaining resolution forms and a reliable model for incomplete programs.

### F40 — Compiler bindings were unavailable after lexical scopes closed

**Status:** Initial source binding index and compiler-backed definitions implemented.

Declaration reflection retained names and line numbers, while local symbol-table
scopes disappeared after checking. Matching an identifier against those names
could confuse shadowed locals or different imported declarations. Matrix's
surface spelling also differs from shared symbol spelling.

**Resolution:** Both tokenizers now retain byte positions. Frontends attach
original-source UTF-8 name spans to explicit AST names; the checker retains
snapshot-local IDs at actual declaration and resolution sites. The opt-in
`--semantic-index` response supplies a bounded `nil.semantic.v1` index for
functions, variables, parameters and their supported references. VS Code Go to
Definition consumes those bindings across dirty and new mixed-language imports,
validating all captured inputs and reached native source snapshots before use.
See [NIL-0010](../../docs/design/NIL-0010-semantic-source-index.md).

**Boundaries:** The index is explicitly partial. Member dispatch, field and
type references, pattern bindings and some function-value/generic forms remain
unindexed. Expanded Ny units need source maps. Binary library dependencies have
no index until library identities and source mappings are represented. Failed
checks, ambiguous occurrences and budget limits produce no navigation result.
References and rename must not treat this subset as a complete occurrence graph.

**Evidence:** Parser source-slice tests and the compiler semantic contract cover
UTF-8/CRLF, lexical shadowing, imports, input routes, failure suppression,
preprocessing, binary dependencies and budgets. Real VS Code tests assert exact
definition ranges and reject stale/unsupported destinations.

### F41 — Matrix function-value spelling bypasses normal name folding

**Status:** Fixed. Paragraph values now use the same declared-name resolution as
ordinary identifier expressions and direct calls.

Given a paragraph named `read-count`, a direct call written as
`evaluate READ_COUNT with 1` resolves its name. A function value written as
`the paragraph READ_COUNT` previously reported `Undefined identifier 'READ_COUNT'`,
while `the paragraph read-count` worked. The latter parser path used `nil_symbol`
directly instead of ordinary `resolve_name` folding. Both now resolve to the
declared `read-count` symbol; the source index still retains the exact written
spelling and excludes backtick delimiters from escaped-name ranges.

**Evidence:** `tests/matrix/function_value_contract.py` checks case/separator
variants, forward declarations, escaped names, lexical shadowing, function-typed
parameters and Ny/Matrix imports through semantic bindings and both textual and
binary NIL execution. The parser source-span test covers escaped folded names
with UTF-8 and CRLF input. External declarations still require their exported NIL
spelling, as ordinary references do: another page's declarations are unavailable
to the parser's local folding table. Native indirect calls remain unsupported.


### F42 — A partial source index cannot establish complete references

**Status:** Certified local references and rename implemented; broader semantic editing remains open.

A list of known bindings does not prove that it contains every use. Renaming a
variable through a missing interpolation, assembly operand, generated token or
named argument can leave a program inconsistent. Named function parameters can
also be referenced by callers outside the checked import graph.

**Resolution:** The compiler certifies eligible body-local variables and loop
binders only after checking retained uses against original identifier tokens.
Missing or ambiguous occurrences with the same spelling withhold certification.
Inline assembly and Ny interpolation conservatively disable local editing for
the source. Functions, globals and all function/lambda parameters are excluded.
The additive `references_complete` and `rename_safe` capabilities enable VS Code
references and local rename without treating the overall index as complete.

Rename uses compiler-provided identifier and reserved-name keys. A new name must
be an ASCII identifier unused anywhere in the file; Matrix folds case and
hyphen/underscore separators for collision checks and preserves existing
backtick delimiters. This intentionally rejects some otherwise legal renames.
The editor checks a speculative source snapshot, compares the full retained
binding graph, then validates the original input set again before returning an
edit. Multiple open aliases of the same file are refused to avoid divergent
buffers. Cancellation or a failed check returns no edit.

**Evidence:** Compiler contracts cover lexical shadowing, assignments, loops,
unused locals, global/parameter exclusions, generated uses and spelling rules.
Editor tests exercise exact references, rename and undo, Unicode/CRLF, Matrix
folding and escaping, collisions, dirty imports, untitled files and file aliases.

**Follow-up:** Add a complete caller/project graph for public symbols and named
parameters and original maps for generated and embedded code. F44 adds an
initial scope query for identifier completion; general incomplete-code recovery
and member/type completion still need broader contracts.


### F43 — Matrix named-argument order can depend on a shadowed paragraph name

**Status:** Fixed through checked callable parameter-name contracts.

A function-valued local named `operation` could shadow a declared paragraph
with that name. In `operation with second-part being 2, first-part being 1`,
the parser used a page-wide paragraph signature to reorder arguments before
lexical resolution. Renaming the local could remove that lookup and change
`102` to `201` while preserving the retained reference graph.

**Resolution:** Matrix now retains raw argument labels on the shared call AST.
The checker orders arguments and their inferred types only after selecting the
actual lexical, imported, generic or member callable. Labels fold ASCII case
and separators against that callable's parameters. Unknown, duplicate and
missing labels fail checking; a named fixed-argument list can precede a `more`
tail only for a variadic declaration. Runtime argument evaluation follows the
resulting parameter order.

Immutable body-local paragraph values retain parameter-name provenance from a
declared paragraph, anonymous paragraph or another proven immutable local.
Mutable callbacks, function-typed parameters and other values without this
contract require positional calls. Structural `fn(...)` types still carry
parameter types without labels. Named variadic callback calls are rejected
because indirect-call lowering does not pack their trailing arguments; callers
can invoke the declared variadic paragraph directly.

**Evidence:** `tests/matrix/named_argument_contract.py` executes the original
shadowing case and its renamed form with deliberately different callback and
shadowed-declaration parameter orders. It covers nested scopes, immutable
copies, anonymous paragraphs, forward declarations, escaped/folded labels,
variadics, Ny/Matrix imports, generic inference and explicit type arguments,
record/class methods and rejected labels/contracts. Checking the real analyzer
example also caught an invalid `index` label for the generated `declaration(id)`
API; its call now uses the declared label. Direct calls run through
native compilation and both textual and binary NIL; callback cases run through
both NIL encodings. The tests establish equivalent runtime results across the
rename, beyond an unchanged reference graph.

**Follow-up:** A labelled function-type contract would allow named calls
through parameters, mutable callback values and returned functions. Variadic
indirect calls need a checked packing convention before they can be supported.


### F44 — Unresolved identifiers need a completion query independent of successful analysis

**Status:** Initial compiler-backed value-identifier completion implemented.

A successful semantic index cannot answer a typical completion request: a typed
prefix such as `sam` is usually unresolved. A declaration list also cannot prove
which local or imported binding is visible at that position.

**Resolution:** `--completion-at` captures visible bindings immediately before
the checker resolves the selected unqualified identifier expression. An unresolved
prefix can therefore return candidates while ordinary analysis still fails.
The result includes every consumed native source, exact UTF-8 replacement offsets,
source-backed candidate kinds/types and insertion spellings. Matrix folding and
escaping follow the frontend resolver. VS Code validates snapshots, rejects stale
requests and replaces the complete token, including a suffix after the cursor.

**Evidence:** Compiler contracts exercise scope exits, nearest shadowing, declaration
order, mixed-language imports, privacy, Unicode/CRLF, escaped names, query budgets
and all analysis input routes. Editor tests cover real completion results, source
changes, imports and exact token edits.

**Follow-up:** The initial query requires parseable syntax and no earlier checker
error. Blank, member/type and qualified-name positions, generated spans, binary
libraries and repeated generic occurrences use tolerant editor fallback. Add
parser recovery, stable generic identities, member/signature queries and expected-type
filtering. Function details currently expose return types rather than complete
signatures. Large standard-library graphs may exceed the bounded query budget;
a persistent source/symbol protocol would avoid retransmitting full snapshots.


### F45 — A source without a module inherited the last imported module's context

**Status:** Fixed in merged source-unit boundaries and verified across checking and execution.

A standalone Ny source can omit `module`, but merging its dependencies into one
AST left the preceding dependency's module active. A root source importing
`helper.*` could therefore call a private `helper` function as though it belonged
to `helper`. Adding `module entry` to that same source correctly rejected the
call. Root globals could also collide with dependency globals because checking
and lowering qualified them under the inherited module. Completion made this
visible by exposing imported private candidates in a normal standalone file.

**Resolution:** The merge records an explicit root-context boundary before each
source unit. Every checker module-tracking pass treats the empty marker as a
reset and creates no empty module symbol; existing code generation applies the
same reset. Explicit module/book declarations then establish their own context
normally. Original source ASTs and reflected declarations do not acquire the
synthetic boundary markers. This changes ordinary compilation as well as
analysis, rather than hiding the problem only in completion.

**Evidence:** `tests/tools/module_context_contract.py` checks source-backed
public imports and distinct consumer/dependency globals through native output,
textual NIL and binary NIL. Both analysis and ordinary compilation reject a
private imported call in Ny with and without an explicit module, and in Matrix
with its required book declaration. `tests/tools/completion_contract.py` keeps
a module-less Ny consumer in its mixed-language import and selective-import
privacy cases. The targeted run passed 14 module-context checks and 46 completion
checks, plus the source-span regression for repeated query occurrences.


### F46 — Member completion must use the receiver's checked type and member visibility

**Status:** Initial compiler-backed member query implemented; import visibility corrected.

A file-wide member list cannot distinguish two local receivers whose fields have
identical names but different types, instantiate a generic field or method result,
or determine which inherited method is accessible. Matrix member names also
intentionally bypass the parser's page-wide folded-name map: an unrelated
unqualified declaration must not redirect a field insertion.

**Resolution:** `--completion-at` captures a nonempty member token after its
receiver has been checked. Candidates are source-backed record fields, methods,
inherited class members and extensions, with generic substitutions from ordinary
member lookup. The result preserves full-token offsets and dependency snapshots.
Matrix prefix matching folds case and separators while the insertion retains the
receiver's declaration spelling, including required escaping. Ordinary member
resolution remains case-sensitive.
The Matrix lexer now recognizes a dot before a backtick as member punctuation;
previously an escaped member such as `` input.`value` `` was mistaken for the end
of a sentence and rejected before checking.

The work exposed a visibility defect: checking an implementation replaced its
member-table entries with symbols that had lost their owning module, and ordinary
member lookup did not enforce imported implementation-method privacy. The checker
now retains that ownership and rejects private imported methods. Completion uses
the same visibility contract rather than merely hiding otherwise callable methods.
Generated `Type__method` registrations also retain method visibility, so spelling
the lowered function name does not bypass the source method's access restriction.
The legacy lookup of manually named `Type__method` functions checks visibility
as well, covering declarations that have no implementation member table.
Both ordinary and generic static dot-call dispatch check access directly after
selecting the method; a type receiver cannot bypass the value-member checks.
Required methods of a successfully checked trait implementation inherit the
trait's access when their implementation omits an explicit public modifier.
Additional helpers retain declared access; correcting privacy does not make
exported trait contracts unusable.

**Evidence:** `tests/tools/member_completion_contract.py` covers receiver
shadowing, generic field/method types, reference/pointer receivers, mixed-language
imports, imported private methods, inherited class members, Matrix `my` syntax,
escaped names, folded-name collisions and Unicode/CRLF replacement ranges. It
compiles applied candidates and checks ordinary-compilation privacy failures.
The focused run passed 66 member checks, 46 existing lexical-completion checks,
50 signature checks, and the trait contract's 100 rejected compilations and 12
accepted controls. Static-call privacy cases cover generic and nongeneric
receivers, callers with and without a module declaration, public controls,
analysis and ordinary compilation, and withheld private signature results.
Clean SDK validation also exposed five cross-book extension operations in the
Matrix execution fixtures that lacked `public`. The documented visibility rule
is per paragraph, so those intended exports now say `public` explicitly. A new
mixed-book Matrix regression accepts a public operation using its own private
helper, withholds the helper from completion, and rejects an external call in
both analysis and ordinary compilation. `matrix.execution` passes with privacy
enforced.

**Follow-up:** Bare dots, type/static receivers, enum members, trait-bound queries,
generated declarations and builtin heuristic members require further contracts.
Wrapped reference/pointer receivers currently offer fields only; method receiver
conversion requires additional coverage.
The grammar defines no block-level export modifier, but the declaration parser
currently consumes and ignores `public` before an implementation or extension
target. Diagnose that unsupported modifier instead of silently accepting it;
exported paragraphs currently require their own `public` modifier.
Parser recovery and a persistent source/symbol protocol remain necessary for
broader incomplete-code support and larger imported graphs. Member case folding
should be resolved by the checker with receiver-specific ambiguity handling.


### F47 — Signature help needs original argument slots and the resolved callable

**Status:** Initial compiler-backed signature query implemented.

A list of reflected declarations cannot identify the callable at a call site,
and a function's semantic display type contains only its return type. Matrix
also reorders named arguments after resolution, so the checked argument vector
cannot identify the parameter under the editor cursor in the original source.

**Resolution:** Both parsers retain the original call argument region, slot
boundaries and Matrix labels separately from the reordered arguments. A bounded
`--signature-at` query selects the innermost call and records the actual function,
source method or proven immutable callback contract. Matrix labels select the
resolved declaration parameter; unknown labels leave the full signature visible
without guessing a highlight. Instance methods omit `self`.

A narrow query-only Matrix recovery supports whitespace immediately after a bare
`with`, while preserving the syntax diagnostic, unsuccessful analysis and absence
of a semantic index. No source text is rewritten, and ordinary compilation still
rejects that incomplete call. Source snapshots, offsets and dependency versions
are validated before the editor shows a result. Temporary compiler failures are
retryable without source edits, and changed inputs cancel pending work.

**Evidence:** `tests/tools/signature_contract.py` passed 50 compiler checks across
both languages, input routes, nested calls, invalid arguments, imports, callbacks,
methods, source provenance and query limits. Eleven real VS Code host checks
exercise member completion and signatures, including unsaved imports, Matrix
label selection, bare-`with` recovery, transient failure and input cancellation.

**Follow-up:** Add general incomplete-call recovery, stable inferred generic and
overload identities, documented builtin signatures and function-type parameter
labels. Immutable callback signatures currently retain canonical parameter names
rather than original Matrix spelling. Project-wide parameter rename also needs
argument-label reference roles and complete caller graphs; query metadata does
not certify those edits. See NIL-0012 and the proposed NIL-0013.

The editor currently requests signatures on spaces in both languages as well as
call punctuation. A caller's cancellation suppresses its result, but a query at
the same cursor can continue occupying the shared compiler queue until it
finishes or times out. Source changes and new cursor queries do abort it.
Measure interactive latency and narrow triggers or add cancellation-aware query
scheduling before extending recovery to more typing contexts.


### F48 — Metadata headers erased native library declaration positions

**Status:** Exact source mapping retained for standalone `@pragma once` and `@schema` lines.

Validation against a copied SDK found an available but empty member query for
`vec<i32>.leng`, despite the source-backed public `length` method and
`length_cell` field. A signature query for the real text library's `trim`
function was also unavailable. The files were native sources with complete
snapshots; lookup and binary provenance were not the cause.

The Ny preprocessor replaces metadata headers with empty lines, while the parser
previously accepted original locations only when all nonblank lines survived
unchanged. One `@pragma once` therefore suppressed every declaration token in
an otherwise unchanged library file. Small fixtures without library headers
had hidden the gap.

**Resolution:** The parser recognizes only exact, standalone `@pragma once` and
`@schema` after horizontal whitespace trimming. Each must emit exactly one empty
line, and every code line must still survive byte-for-byte. The existing line
offset map restores original UTF-8 name, call and argument-slot positions across
the removed header text. It does not report offsets into the shorter processed
source. Other pragmas, parameterized markers, macros, includes and conditional
rewriting remain unavailable without a general preprocessor source map.

**Evidence:** Parser and query contracts cover metadata headers in primary files
and imported dependencies, Unicode/BOM, LF/CRLF, missing final newlines, generic
fields and methods, Ny/Matrix consumers, signature argument slots and compilation
of completion insertions. Negative controls retain the previous behavior for
unsupported transformations. The focused run passed 79 member checks, 46 lexical
completion checks, 57 signature checks and the source-span unit tests. Six actual
standard-library queries also pass in both languages: `vec<i32>.leng` exposes
`length` and `length_cell`, `trim` identifies its first parameter, and
`try_substring` selects the correct parameter across Ny positional and reversed
Matrix named arguments. Every native snapshot matches the corresponding source
bytes across the 30-, 31- and 14-file dependency graphs. SDK qualification repeats
these queries against a fresh copied SDK after sealing the corrected compiler.

**Follow-up:** Add a full preprocessor source map before exposing locations from
includes, macro expansion or conditional rewriting. Metadata directives with
arguments or comments remain outside this deliberately narrow mapping contract.

### F49 — Numeric signs should read as values, while subtraction stays explicit

**Observation:** Prefix `minus 1.6` was the only Matrix spelling for a negative
floating value, even though the operation describes the value's sign. The
punctuation rule also rejected the familiar `-1.6` notation. Hyphenated names
make a general symbolic subtraction operator ambiguous with existing source.

**Resolution:** `negative` is now a unary alias for `minus`; `minus` retains its
infix subtraction meaning. A `-` immediately followed by a digit is accepted as
a numeric sign, including hexadecimal integers and scientific notation. Thus
`negative offset`, `-1.6` and `-1.6e-3` are valid, while `left negative right`,
`left - right`, `- offset` and `-.5` are rejected. Write `-0.5` instead of `-.5`.
Hyphenated names and their separator-folding behavior are unchanged.

The forms share existing integer-boundary validation and unary lowering, rather
than introducing signed literal tokens with a different range or arithmetic
contract. They apply in ordinary expressions, numeric match patterns and
parameter defaults. `negative` and `tuple` also enter the compiler's reserved
rename keys, so editor renames cannot introduce newly reserved names silently.

**Coverage:** The signed-number route fixture exercises the 32-bit and 64-bit
minima, hexadecimal minima, subexpressions, arrays, calls, scientific exponents,
statement/expression patterns and signed zero distinguished through `atan2`.
The rejection contract covers malformed and overflowing literals across compile
modes and editor analysis, checking that existing output artifacts are retained.
Syntax grammar and language-service tests cover the new keyword, signed number
spans, scientific type inference and preservation of hyphenated identifiers.
The focused run passed 480 signed-number rejection/analysis checks, 42 language-service
protocol tests, 100 extension unit tests and 8 actual VS Code host checks covering
interpolation and tuple editing alongside the new literal forms.


### F50 — Tuples need one structural type and representation across both languages

**Status:** Implemented for nonempty typed tuple values, local destructuring
and typed tuple arrays in the interpreter and native routes.

The old checker and interpreter contained tuple-like `zip` and array-unpacking
heuristics, while the surface documentation correctly withheld general tuples.
Exposing that scaffolding as a language feature would have allowed native and
interpreter values to disagree. Ny `(T, U)` and Matrix `tuple of (T, U)` now
share canonical ordered NIL record schemas with immutable numeric slots. Both
frontends construct `(a, b)`, destructure local bindings, and support nested
projection and tuple match patterns. Constructors evaluate once in written
order, and contained record/reference handles preserve their identity.

**Friction addressed:** `(value)` remains grouping, `(value,)` is a singleton,
and `.0.1` is tokenized as nested positions rather than a decimal. Huge or
out-of-range positions diagnose without throwing from host integer parsing.
Tuple literals can convert elements at construction boundaries; existing
tuples cannot silently change their storage layout.

**Qualification:** The tuple contract covers both frontends, executable text
and binary NIL, bundles, native routes, and cross-language consumers using
source and binary libraries with the original source files removed. It also
checks failure before replacing output.

**Follow-up:** The tuple-array storage and loop contract is implemented in F52.
Nested declaration patterns, named elements, unit values, user-defined tuple
formatting and C ABI mapping remain explicit future work. See
[NIL-0015](../../docs/design/NIL-0015-structural-tuples.md).

### F51 — Interpolation needs typed conversion and original expression spans

**Observation:** Ny already recognized implicit identifier substitutions in
ordinary strings, but the implementation treated arithmetic as literal text,
misdecoded some doubled braces and emitted a `TO_STRING` operation unsupported
by native compilation. Its identifiers were synthesized from decoded string
contents without original source positions, which forced the editor to withhold
local rename for any Ny file containing a brace in an ordinary string. Matrix
quoted strings and raw text blocks deliberately had no interpolation.

**Resolution:** Explicit Ny `$"..."` and Matrix `formatted "..."` accept ordinary
expressions inside braces. Both frontends emit the same interpolation AST.
Their lexers retain original byte positions for every expression token instead
of parsing decoded literal text, so escaped prefixes, Unicode, CRLF and nested
quoted arguments cannot shift definitions, signature regions or rename edits.
The checker and runtime use a typed scalar/text conversion shared by native and
interpreted execution. Each expression is evaluated once, from left to right.
Legacy implicit Ny floating interpolation consequently changes from fixed six
fractional digits to the same shortest round-trip representation. Native text
retains its NUL-free restriction, including formatted literal segments.
Generic interpolation records a scalar/text requirement on its declaration.
Resolved call substitutions propagate that requirement through generic wrappers
and receiver methods, so editor analysis can reject unsupported concrete
record, array or tuple arguments at the callsite before code generation.

Doubled braces insert literal braces in explicit formatted text. Ordinary
Matrix quoted strings and `begin text` blocks remain raw; legacy Ny identifier
interpolation stays available for compatibility. `formatted` is contextual
before a quote and remains an ordinary Matrix name elsewhere. Explicit strings
and their expressions currently occupy one physical source line. Empty holes,
trailing tokens, unmatched braces and unterminated strings are errors; the
parser does not silently convert malformed expressions back into text.

**Coverage:** Source-span tests include decoded Ny prefixes, raw Matrix
backslashes, nested formatted strings, quoted expression arguments, UTF-8,
CRLF, exact locations after the string and malformed input. Compiler query
contracts exercise completion and signature help inside holes. The semantic
contract checks local references and applies an actual rename, proving that
matching words inside doubled-brace literal text remain unchanged. Execution
contracts cover the shared scalar conversion and evaluation order.

**Follow-up:** Add multiline formatted text only with an explicit original
source map through indentation removal. Custom record formatting should use
the existing `Display` contract once dispatch is qualified on all routes;
currently callers must explicitly produce a supported scalar or text value.
Precision and presentation options remain library calls rather than a second
formatting grammar. The legacy implicit form still lacks original embedded
name spans and retains its conservative editor exclusion.

### F52 — Tuple arrays need exact schemas, managed lifetime and explicit bounds

**Observation:** Typed tuple values alone did not make collections of tuples
portable. Native arrays had scalar element kinds only, generic array intrinsics
rejected tuples, and interpreter tuple-loop support could fall back to generic
iterator behavior. Disabling the source type checker also allowed mixed tuple
schemas into arrays. Treating an invalid read as scalar zero could then appear
to produce a tuple that had never been constructed.

**Resolution:** Tuple arrays now have an explicit native element kind carrying
managed tuple handles and their exact ordered schema. Nominal names inside a
tuple retain their case. The VM checks the annotated schema and initialization
of stored tuples; native lowering and runtime tracing preserve tuple handles
and their reachable text, record and reference values. Empty arrays carry the
same schema as populated ones. Source mismatches reject before replacing an
output artifact, including when `--no-typecheck` is requested. Fresh tuple
literals can apply checked scalar conversions at declared construction
boundaries; existing tuple values and array types remain invariant.

Raw NIL must also prove each tuple slot's declared kind and nominal identity.
An enum value cannot stand in for a record, different concrete generic enum
types cannot alias through a shared variant name, and an integer is not proof
of a live allocation. Pointer checks verify the allocation type and alignment,
including null and logical one-past values. Positive source cases preserve declared choices and live
typed base/interior/one-past pointers without changing pointer ownership rules.
Textual NIL loading now retains enum variants and payload types. Intrinsic
results retain concrete enum metadata, including generic `Result` values;
older annotated wrappers can recover that identity from their declared return.

Indexed reads, whole-element replacement, append, generic new/get/set/length
and checked range copies share that representation. Array aliases observe
mutation and growth; extracted tuple values keep their immutable slots and
contained reference identities. Tuple loops evaluate the selected array once,
including direct paragraph returns, and retain ordinary binding scopes,
wildcards, `break` and `continue`. Rebinding the source name does not retarget
the active loop.

Out-of-range tuple reads and uninitialized tuple slots fail explicitly. There
is no synthetic default tuple. Out-of-range writes remain no-ops, and existing
scalar-array read defaults are unchanged. Native arrays whose elements are
records, choices or other arrays remain excluded; support for tuple handles
does not establish a general aggregate-array ABI.

**Coverage:** Paired Ny/Matrix tuple-array fixtures and
`tests/tools/tuple_array_contract.py` specify source and binary execution,
bundles, native compilation, cross-frontend source/binary-library consumption,
generic standard-library calls, overlap copies, schema failures and bounds.
The tuple contract includes direct, named and returned array loops. Native
runtime tests inspect aliasing, growth, transitive collection and reclamation.
Raw text/binary NIL tests cover malformed nominal fields and initialization,
legacy intrinsic wrappers, and collection at the tuple-initializer allocation
boundary.

**Follow-up:** Extend other aggregate-array layouts only with equally explicit
schema, initialization and lifetime contracts. Nested declaration patterns,
custom formatting and native FFI layout remain separate work from storing
typed tuple handles.
The VM still represents raw pointers as integers: a matching virtual address
can establish a live allocation, but cannot establish the value's provenance.

### F53 — Memory diagnostics need explicit accounting and capture boundaries

**Observation:** Aperture's initial memory command returned placeholder
snapshots. Runtime allocation counts alone cannot establish leaks: retained
values may still be reachable, and VM values and native allocations have
different storage representations. A report taken after shutdown cleanup also
hides the storage developers need to investigate.

**Resolution:** Opt-in sessions record real allocation, resize, release and GC
activity and finalize before runtime teardown. VM and native source execution
share the versioned `aperture.memory.v1` report contract, with source attribution,
completion status, explicit scope and bounded tracking. Counters obey
`allocated - freed = live`; peaks describe simultaneous live storage. Aperture
preserves target output and exit status, refuses to overwrite reports, and
rejects missing or malformed measurements. VS Code can run either route and
open the saved report.

**Follow-up:** VM text, record and host-container storage remain outside this
first accounting scope. Native descriptor/capacity measurements differ from VM
payload bytes. Neither represents process RSS. Full call stacks, retention
graphs, allocation lifetimes, parallel-session aggregation, CPU sampling and
debugger snapshots need separate contracts. The first capture owns one runtime
session; restarting a paused VM must mark its old capture abandoned rather than
silently combining executions. See the [Aperture design](../../aperture/docs/DESIGN.md).

### F54 — Native system-call arguments and console input needed route parity

**Observation:** Aperture's real-program qualification exposed two existing
native limitations. `nil.std.system.exit(7)` passed source checking and worked
in the interpreter, but native compilation required an explicit `as i32` cast.
Direct native dispatch bypassed the declared wrapper's scalar parameter
boundary. Separately, native lowering did not implement the reachable
`console.read_line` and `console.flush` intrinsics.

**Implemented:** Source lowering now normalizes declared scalar parameter
representations before direct annotated interop/intrinsic dispatch. Literals,
variables, returned values, reordered Matrix named arguments, and declarations
loaded from source or binary libraries use the existing scalar conversion
semantics. Integer narrowing wraps as it does at other scalar storage
boundaries. Floating-to-integer conversion requires an explicit source cast;
its nonfinite and out-of-range checks remain intact. Boolean and non-scalar
operands retain their representation so provider validation can reject them.
In particular, implicit conversion must not make a fractional sleep duration
silently succeed. Raw NIL and forged provider declarations must still satisfy
exact native ABI checks. This is a lowering fix; broad scalar compatibility
diagnostics in the analysis/typechecking API remain a separate task, including
VM values whose handle representation alone cannot prove their source type.

Native line input and flushing now have runtime, wrapper, direct NIL, and JIT
implementations. Line input strips LF, preserves an empty line or a final
nonempty unterminated line, and faults on EOF or input errors. On POSIX, CR is
retained. UTF-8 bytes are preserved. Native text's existing NUL-terminated ABI
requires explicit rejection of embedded NUL, while the VM retains that byte.
Reading publishes a preceding console prompt before blocking. Returned native
text is managed storage that survives collection when rooted and appears in
Aperture allocation sites. Console contracts exercise Ny and Matrix, both
memory modes, long input, prompt visibility, failures and invalid ABI metadata.
Aperture exercises stdin forwarding and EOF failure reports on both routes.

**Follow-up:** The public `nil.std.runtime.collect`, `stats`, and critical-section
wrappers still need a coherent native API. Native managed safepoints already
collect automatically, and Aperture observes reclaimed storage. Simply exposing
the internal collection helper would be misleading: it deliberately does
nothing without compiler-managed root frames. Public collection also needs
safe/unsafe mode checks, generation validation, nested critical sections, and
per-context counters with defined reset and overflow behavior.

Public GC statistics and Aperture measure different scopes. VM GC statistics
count raw buffers and eight-byte array slots; native managed storage includes
text, records, descriptors and capacity, while raw allocations are separate.
Define those units and the returned statistics object's own allocation before
claiming cross-route equivalence. Windows console behavior still needs platform
qualification.

Imported library allocation sites can retain a function name without a source
file when the library lacks debug metadata; a full caller stack is not captured.
An embedding host that enables the C++ input stream's `eofbit` exception mask
can also turn a valid final unterminated line into an error. Normal CLI stream
defaults are covered; custom exception-mask behavior needs a shared VM/native
input policy and a separate embedding contract.

### F55 — VM array growth omitted cumulative allocation statistics

**Observation:** Appending to an array increased `current_heap_usage` but left
`stats().total_allocated` unchanged. Growing an initially empty array could make
live bytes exceed cumulative bytes, including through an alias.

**Implemented:** Each successful append charges one existing eight-byte GC
array slot to the cumulative counter. The shared accounting helper saturates
at the public signed 64-bit limit. Element replacement, explicit free, and
collection do not add or subtract cumulative allocation bytes. Safe and unsafe
VM contracts cover repeated growth, aliases, `i32`/`u8`/`bool` arrays, raw buffer
accounting, and reclamation after frames depart.

**Follow-up:** This preserves the public GC statistics' historical slot unit.
Aperture's typed payload measurement remains independent; it must not be
silently substituted for the GC API's byte counts.


### F56 — Readable patterns need a precise profile and honest resource errors

**Observation:** The old experimental regex declarations had no qualified runtime
provider. Treating familiar regex dialects as interchangeable would hide real
semantic differences, especially in lookahead, Unicode classes and captures.

**Implemented:** NIL-0016 introduces one shared, bounded byte-pattern engine,
consumed by conventional notation and Ny/Matrix prose declarations. Literals are
escaped; captures, repetition and zero-width lookahead use an explicit grammar.
The library separates no match from invalid input and exhausted budgets, and
preserves absent versus participating empty captures. Next-match iteration makes
progress after empty matches. The compiler checks declaration sources, capture
labels and exact contextual delimiters; a quoted/escaped label cannot inject
regex syntax. Compiled programs are immutable and cached within fixed limits.

**Follow-up:** Native text remains NUL-free and byte-oriented. Unicode properties,
word/grapheme boundaries, alternate dialect parsers, reusable named fragments and
replacement templates need explicit contracts. Collection, splitting and literal
replacement are now implemented with explicit bounds (F59). Named
prose declarations currently produce immutable text sources; they do not infer a
first-class compiled Pattern value. Engine cache/backtracking budgets account for
native program/state storage rather than exact host RSS or Aperture allocations.

Matrix's unparenthesized nominal generic type grammar makes a nested multi-argument
shape such as `Result<Option<PatternMatch>, text>` difficult to express without
ambiguity. The initial pattern wrapper is written in Ny and imported normally
from Matrix. Add an unambiguous nested type surface rather than proliferating
wrapper types to conceal this grammar friction.

### F57 — Query composition needs evaluation, ordering and lifetime contracts

**Observation:** Existing callback-based collection helpers and experimental LINQ
sources did not establish native closure lifetimes or query semantics. Native
arrays still exclude direct records, enums and nested arrays, so a broad generic
Enumerable promise would overstate the supported storage model.

**Implemented:** Collected queries share one checked AST and ordinary NIL loop
lowering. Source and bounds execute once before a shallow snapshot. Filtering,
stable one-key ordering, pagination and scalar/text/tuple projection work from
both languages. NaNs sort last in either direction; ties preserve input order.
Matrix-authored count/any/first/last/single terminals consume the resulting array.
The binder has an exact source span and lexical scope for editor analysis.

Repeated row expressions also need the ownership checker's loop invariant.
Review found the query checker omitted it; the analyzer now rejects changes to
outer owners across row evaluation, including empty sources and take zero.
Current code generation already rejects the block-tail expression used to expose
the analysis gap, so this was not evidence of an executable double-move path.

**Follow-up:** Grouping, joins, distinct, multiple order keys, lazy iteration and
SQL translation remain separate work. Aggregate-array support is still needed
for ordinary arrays of application records. The current snapshot and sorted
query allocate temporary arrays; profiling and optimization should preserve
once-only effects and stable ordering rather than silently changing semantics.
Generic helper bodies also reject query sources such as `array<T>` before
specialization, even when an actual call supplies a supported scalar type. A
future implementation must validate the concrete element profile after
specialization; current concrete-type support does not imply generic query
composition.

### F58 — Nested enum constructors lost payload type context in native lowering

**Observation:** `Result<Option<T>, text>.Ok(Option.None)` passed source checking
but emitted a generic inner variant without a concrete enum schema. The VM could
run it, while native lowering correctly refused the incomplete metadata. Pattern
search's `Result<Option<PatternMatch>, text>` exposed the gap.

**Implemented:** Enum constructor lowering resolves concrete payload types before
emitting argument expressions. Member, qualified and built-in alias constructor
paths retain the nested type context and source argument evaluation order. The
native enum fixture now exercises absent/present nested options, a floating
payload, multiple nested payloads, and side-effect ordering.

**Follow-up:** Multi-payload enum construction is covered, but destructuring a
multi-payload variant in a match is still unsupported. The current checker can
misreport each binding as the combined `(T, T)` payload, while code generation
explicitly rejects multi-payload pattern lowering. Implement component typing,
extraction and diagnostic consistency together; the current constructor fix does
not imply that pattern support exists.

### F59 — Pattern collection needs complete results and explicit empty-match behavior

**Observation:** First/next matching provided the mechanism for text processing,
but every caller had to rebuild collection, splitting and replacement loops.
Using the advancing search offset as an output cursor would lose bytes after
empty matches. Repeated text concatenation also copied the growing output prefix.

**Implemented:** `find_all_matches`, `split_matches` and `replace_matches` share a
checked whole-match scan. Empty matches remain observable; splitting preserves
empty fields and excludes captures; replacement text is literal. Search and
output positions remain separate. More than 4096 matches or a later search
failure returns an error, never a partial successful result. Replacement checks
its input and complete output against 1 MiB before balanced concatenation.

**Follow-up:** Each match search retains its own engine work budget. A shared
operation budget, streaming consumers, replacement callbacks/templates and
Unicode-aware progress need separate contracts. Collection returns whole-match
text because native arrays of `PatternMatch` records remain unsupported; callers
needing per-match captures can continue using first/next matching.

### F60 — Indexed values lost their checked element type before lowering

**Observation:** Even `items[0] + items[1]` on a plain text array passed analysis
but failed lowering as a numeric operation. Indexed text methods such as `.len()`
could emit an unresolved call. Pattern-result composition exposed this broader
gap; it did not depend on regex or generic Result types.

**Implemented:** The index-expression checker now preserves its resolved result
type on the AST for lowering. Regression coverage includes concatenation, text
methods, comparisons, returned and unwrapped arrays, and once-only index effects.
Ny and Matrix pattern fixtures also retain direct indexed operations.

### F61 — Binary-only library analysis lacks editor symbol data

**Observation:** The sealed SDK's source-backed imports provide completion for
all three new pattern helpers from Ny and Matrix. Restricting lookup to `nillib`
still type-checks complete calls successfully, but semantic analysis returns no
symbols and completion requests return no completion result. Runtime consumer
tests with binary libraries pass, so this is an editor-analysis limitation.

**Current boundary:** The SDK includes `nilstd` source and the normal editor
workflow uses it; the real VS Code host checks pass with that layout. Binary-only
third-party libraries cannot currently provide equivalent completion/navigation.

**Follow-up:** Preserve declaration identities, types and navigable interface
locations when analyzing imported NIL libraries. Test completion and semantic
navigation against a copied binary-only dependency with its source removed,
including both frontends and incomplete calls. Do not treat successful binary
compilation as evidence that its editor metadata is available.
