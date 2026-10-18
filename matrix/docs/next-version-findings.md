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
[no escape syntax](types-and-text.md#text-has-no-escape-syntax); `\n` remains
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

**Status:** Formatter fixed; broader conversion parity remains open.

A pure Matrix diagnostic formatter using `Convert.char_to_i32`,
`Convert.i32_to_char`, and `Convert.to_hex_padded` compiled and ran in the VM but
failed native lowering. The shipped helper now uses portable text operations
and small Ny control-character constants. The paired Ny/Matrix fixtures exercise
UTF-8, quotes, backslashes, control escaping, and declaration values through all
six routes. Do not infer native support for every `Convert` intrinsic from this.

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

**Status:** Confirmed authoring/diagnostic gap; no inference change in this pass.

**Evidence:** Passing `-9223372036854775807 - 1` to an `i64` parameter produces
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

**Workaround:** Bind `let maximum: i64 = 9223372036854775807` in Ny or
`value maximum as number is 9223372036854775807` in Matrix, then construct the
minimum with `-maximum - 1` or `minus maximum minus 1`. The paired RNG fixtures
use this form and verify the true i64 extrema against independent oracles.
An explicit cast on the literal operand also fixes its arithmetic width.

**Next step / acceptance:** Define literal-range and contextual-inference policy
for both frontends. Diagnose values outside an inferred integer width, or adopt
specified wider inference; keep explicit wrapping arithmetic distinct from
accidental narrowing of a literal. Matrix's intended `number` default needs
matching inferred expression/array behavior and documentation. Cover nested
arithmetic, calls, arrays, editor diagnostics and all execution routes before
changing the language contract.
