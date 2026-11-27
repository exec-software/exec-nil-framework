# Language, library and development hardening

Work is in progress. The user requested compiler correctness, collections,
native capability, tooling and distribution improvements in that order, with
mainline consolidation last. No stable release or SDK activation is implied.

This work starts from the integrated Goetic branch at
`87cbb1e2097cc3cb3adc837cc146a23300a11feb`, which includes the separately tested
Windows durability and editor dependency fixes. It does not merge into master.

## Work sequence and acceptance

1. Correct F93 Matrix lexical name resolution and F21 Ny definite assignment.
   Retain failing baseline evidence, test valid scope/control-flow patterns and
   reject bad source before either interpreter or native execution. Completion
   must use the same lexical scope at the requested source position.
2. Replace shifting queue removal with a ring buffer and linear HashMap lookup
   with bucketed lookup. Preserve documented ordering and alias behavior.
   Implement real array reservation/capacity in both runtimes before changing
   vector capacity promises. Test collisions, wraparound, growth, replacement,
   bounds and reuse; report any remaining linear operations honestly.
3. Extend native application capabilities through individually specified ABI
   contracts. Capacity operations are the first additive array contract. Native
   aggregate arrays/globals, callbacks and binary networking need separate
   implementation and execution evidence; they are not closed by scalar-array
   tests or by a declaration compiling.
4. Improve editor/compiler iteration using actual incomplete-source and project
   fixtures. Scope-correct completion accompanies F93. Source provenance, build
   reuse, interactive debugger stdin and native attach remain separate work.
5. Extend the existing observed-capability catalog and installed consumer tests
   with the new contracts, then qualify identified SDK candidates. Keep source,
   installed SDK, desktop and stable-release evidence distinct. Web/WebView
   distribution compatibility must be checked against the selected framework.
6. Consolidate mainline only after the preceding changes are reviewed and
   qualified. A merged intermediate PR alone is not proof of inclusion in
   master or an installed consumer SDK.

## Initial implementation decisions

Array reservation requests an absolute minimum capacity without constructing
elements, changing length or replacing the shared handle. Negative and
out-of-range capacities fail; allocation failure must preserve prior contents.
Capacity is measured in element slots and may exceed the requested minimum.
Because capacity becomes observable state, interpreter checkpoints must preserve
it and charge retained reservation storage against their existing byte budget.

The queue uses shared head/count state so mutation through a passed value remains
visible to its aliases. HashMap keeps its existing dense public key/value order;
bucketed lookup does not imply constant-time order-preserving removal.

All implementation and validation results below must identify their actual
source revision or working-tree snapshot. Existing accepted SDKs remain intact.

## Implementation and review

The Matrix pre-scan now records module declarations only. Lexical scope ranges
and declaration activation offsets drive both parsing and cursor completion.
The retained baseline reproduces F93's unresolved `Error` call; the corrected
fixture passes six execution routes. Completion retains partial-buffer behavior.

F21 uses a shared lexical must-analysis after type resolution. Reads identify
their source location and declaration line. Literal constant branches also
prune infeasible edges in native local/field initialization checks; unknown
branches remain conservative. Review additionally found valid exhaustive
matches and exact typed-handler cleanup that need equivalent source/native
proofs. The fixes retain checked bool/enum/tuple coverage and distinguish exact
explicit faults from unknown call effects. Native lowering derives optional
match facts from validated NIL's actual fault sources, separately from the
synthetic edges used to validate handler admission. Unknown or mixed matches
remain conservative; this is not general interprocedural fault inference.

The same review found a preexisting Ny arm-boundary bug: a guarded or tuple arm
after another arm could be consumed as part of its predecessor. Bounded pattern
lookahead now recognizes those boundaries without scanning through a guard or
borrowing an arrow from a later statement. Parser controls and a six-route
execution fixture also preserve ordinary grouped-expression/call bodies.

Array `reserve`/`capacity` use additive runtime entry points and existing NIL
CALL adapters, preserving handles and length. Native descriptors remain checked
against the current runtime context. The loader adapter requires a proven array
receiver, and source calls reject incorrect arity and noninteger capacities.
VM checkpoints admit only these two new memory-only builtins; other builtin
admission rules remain in place. Reserved storage is cloned and budgeted.

The ring queue materializes initialized slots on first enqueue, so that first
operation can take O(reserved capacity). Unused slots retain old values until
overwritten or collected. This supports element types without a default value
without fabricating invalid instances. HashMap lookup/insertion uses buckets;
ordered removal remains O(n), and keys must keep stable hash/equality behavior.

## Validation record

Evidence is retained outside the source tree under
`hardening-20261126-221pc1uo`. Initial focused checks pass for the array ABI,
VM array identity and checkpoint reservation/byte-budget behavior. Collection
reports contain 22 passes: six each for the Ny queue, Matrix queue and scalar
HashMap, plus four interpreted/bundle passes for record collections.

The capacity contract rejects 45 malformed native NIL shapes, including 14 new
cases, and checks 12 source errors through textual and binary output. It retains
failed diagnostic-wording evidence as well as the passing rerun. Source booleans
cannot be reserve counts; raw VM scalar representations retain their existing
type-erasure limitations.

The first complete Linux LLVM run had 614 passes, three skips and one fixture
registration failure among 618 tests. The Matrix execution runner expected a
`.expected` file; the new queue oracle now uses that name. Missing PostgreSQL
headers/server configuration and Xvfb account for the skips. This is development
coverage, not a new RC or cross-platform SDK qualification.

The Matrix queue also has the runner's required `.needs-stdlib` marker. With
both fixture declarations corrected, all 40 Matrix execution fixtures pass.
The original typed-recovery and bool/enum/tuple review programs now pass 12
interpreter/JIT/AOT runs. Nested recovery transfers coalesce before analysis;
the source contract includes a bounded twelve-level regression.

Matrix scope/completion was checkpointed and pushed separately as `929dda9`.
The remaining changes stay on `codex/language-library-hardening`; mainline
consolidation is intentionally deferred.

The final focused assignment run passes 41 negative programs through normal
compilation and analyzer diagnostics, eight Ny/Matrix execution routes and
eight native admission controls. The native fault proof passes 36 positive
routes, 44 rejection controls and four terminal-call compilation controls.
Literal-dead calls preserve exact handler proof; live and runtime-conditional
calls remain unknown. Full committed-source and installed-candidate results
will be retained as external qualification artifacts.

The first committed-source run (`bca511f`) passed 615 of 619 tests with three
provider skips and one stale generic-enum bytecode-shape assertion. Exhaustive
matching legitimately omits the last comparison. The metadata fixture now puts
the required variants first, preserving every metadata/debug-line assertion
and exact output. Its focused rerun passes; the original full-run evidence is
retained before repeating qualification against the corrected commit.

## Development candidate results

The clean `bb82f5d` run passes 616 CTests with the three recorded provider skips
and no failures. Its separate SDK candidate validates 11 root-level tools,
19 strictly checked binary STL modules, source STL, 76 documentation pages and
279 sealed files. All 1,328 installed consumer executions pass against both
source and binary libraries. Installed durable-I/O checks also pass.

The bounded Web compatibility check compiles its full test source in four
source/binary-STL and text/binary-NIL combinations, then passes 393 pure
SHA-256/framing checks in each of four interpreter combinations. WebView passes
eight synthetic-provider interpreter/JIT/AOT routes. The SDK and both repos
remain unchanged. HTTP wire behavior and actual desktop/browser qualification
are separate from these checks; no existing SDK was activated or replaced.

GitHub's LLVM-disabled run found a registration error: the new native fault
test requires the optional backend. CMake now registers it only with LLVM
enabled. This follow-up changes test configuration, not the `bb82f5d` SDK's
language/runtime/library contents. The existing candidate stays pinned to that
tested revision; mainline consolidation and broader platform qualification
remain open.

The capability catalog now declares 18 observed groups and 36 proof fixtures;
its validation tests pass. The installed consumer set grows by five fixtures
to 83 (1,328 source/binary-library execution and assembler/linker runs). Final
candidate construction must record clean source/build identity before its full
CTest run, then package and validate tools, binary STL, source STL and docs
without activating or replacing an existing SDK.


## Record-array and editor follow-up

The earlier source-validation run at `b01e9b3` completed successfully. Built-in
array member completion is committed separately as `8a934fb`: seven checked
methods now appear without invented source declarations, while source-backed
completion keeps its declaration/snapshot checks. The expanded member contract
passes 114 checks and six surrounding analysis/editor contracts pass.

The next F07 slice adds native arrays of concrete declared non-class records.
Records retain the interpreter's shared mutable object identity. Filling,
copying and reading elements preserve that identity; replacing a slot does not
replace an extracted record. A distinct runtime array kind owns its exact
resolved type descriptor, including namespace, case and generic arguments.
LLVM validates that descriptor before each operation, and requires proven
initialized records before storage. Empty arrays materialize their concrete
schema even when no constructor or function signature otherwise uses it.

The interpreter now validates record array reads/writes and empty element
schemas. Missing record reads fail instead of yielding scalar zero. Existing
scalar default reads and valid-value out-of-range write no-ops remain intact.
Native GC traces record graphs; ABI tests cover descriptor ownership, growth,
transitive text/array/record roots, reclamation and failed-allocation accounting.
Raw NIL controls reject wrong identities, unknown/duplicate schemas, an enum
and record sharing a name, incomplete native objects and forged handles.
The raw-pointer classifier remains restricted to its existing integer kinds.

Ny and Matrix fixtures cover storage, aliases, replacement and generic
primitives. The Ny fixture adds nested fields, returned graphs, overlap copies
and empty generic specialization. The record-key HashMap and no-default record
queue now pass six routes. HashSet/Dictionary copy tests join List/Queue/Stack
on VM/JIT/AOT: 285 cases pass. F07 remains open for enum/choice, nested-array,
class/reference-wrapper elements and recursive schemas. Aggregate globals,
callbacks, binary networking and broader platform qualification are separate.

The capability catalog adds a record-array observation group (19 groups,
39 proof fixtures, 234 six-route observations); installed consumer coverage now
includes 86 fixtures. A fresh development candidate must be bound to clean
source/build evidence and a complete CTest run before packaging. The prior SDK
candidate and active SDK remain unchanged. Mainline consolidation stays last.


The first `18608ab` full run completed with 618 passes, the same three provider
skips and one tuple-diagnostic assertion failure. The new early element-schema
check rejected the invalid tuple correctly but omitted its tuple context; the
recursive diagnostic now retains that context without changing the assertion.

An additional imported generic-record probe passed all eight unqualified and
qualified text-VM/binary-VM/JIT/AOT combinations. It exposed a source alias bug:
a generic alias constructor nested directly inside an array intrinsic retained
its alias spelling, while assigning it to a local supplied the correct nominal
schema. Constructor checking now resolves aliases before validating the
underlying record/class fields and publishes the resolved identity for lowering.
The fixture covers nested intrinsic/call/field constructors and i64/f64 fields;
negative controls require actual type-checking diagnostics for wrong, unknown,
missing and incorrectly scoped fields. The empty-only generic schema fixture
uses a separate type so these added constructors cannot mask its regression.
The full suite and SDK qualification will be repeated from the corrected clean
revision; the first run and staged files remain preserved as failure evidence.


## Verified record-array development candidate

The corrected clean revision `95d7194a993a60c99ee63feea0539360594fe7ee`
passes 619 of 622 local CTests with zero failures. The three skips are the
PostgreSQL client/provider and graphical-display dependencies unavailable on
this host. The focused record-array contract now passes 119 checks; imported
unqualified, qualified and explicitly aliased generic-record probes pass all
12 text/binary-VM/JIT/AOT combinations. The candidate is development-qualified,
not a stable release or a new Windows/macOS parity claim.

Packaging validates 11 root-level tools, 19 strictly checked binary library
modules, the source standard library, 76 documentation pages and 279 sealed
files. Its observed capability record is verified for 19 groups and 39 proof
fixtures. All 1,376 installed consumer executions pass: 86 fixtures against
source and binary libraries, with six execution routes plus text/binary NIL
through the standalone assembler/linker. Installed durable-I/O checks pass.

Bounded compatibility checks compile the full Web test source in four
source/binary-STL and text/binary-NIL combinations, pass 393 pure SHA-256/framing
assertions in each of four combinations, and pass eight synthetic WebView
interpreter/JIT/AOT routes. SDK, provider and repository pins remain unchanged;
the checks open no server or desktop windows. The active SDK and earlier
candidate are unchanged. The new candidate remains pinned to `95d7194`; this
notebook's subsequent evidence-only commit does not rebuild or relabel it.


[GitHub Source validation](https://github.com/exec-software/exec-nil-framework/actions/runs/45929987474)
passed for exact `95d7194`: LLVM enabled passes all 622 tests with zero skips;
LLVM disabled passes 470 with 145 expected native-only skips and zero failures;
VS Code passes. Both configurations verify actual backend selection. This
remote evidence does not change the locally packaged candidate's development
qualification or substitute for Windows/macOS installed-consumer checks.

## Enum and nested-array follow-up

The next F07 slice adds native enum/choice elements and finite nested arrays.
Both use additive typed-array runtime entry points with owned exact element
identities; the earlier record ABI remains available. Enum words retain the
existing variant representation. Nested stores require a live child array in
the current runtime context with the expected rank and element kind/identity.
Tuple children retain their existing compiler-proven shape contract. Native
collection follows both enum payload graphs and nested array descriptors.

Array element types remain invariant. Extracting an enum preserves its variant
when a slot is replaced, while its mutable record/array payloads remain shared.
Outer array copies share their children; replacing a child does not replace an
already extracted child. Enum and nested-array reads cannot fabricate a default
variant or child: invalid reads now fail on both interpreter and native routes.
Scalar defaults and valid-value out-of-range write no-ops remain unchanged.

Concrete enum payload schemas are materialized even for empty-only arrays,
including generic records, generic enums and tuples behind a nongeneric enum.
Native schema validation follows array indirections to reject unsupported
recursive nominal graphs. Loader provenance checks preserve class/anything
payload identity through nested arrays and checked operations without allowing
binding-reference cells or opaque calls to erase that proof.

New paired Ny/Matrix fixtures cover variants, jagged arrays, aliases,
replacement, reserve/growth, generic primitives, overlapping copies and returned
managed graphs. Focused controls cover text/binary NIL identity mismatches,
invalid reads, forged handles, sensitive payload provenance, recursive schemas
and both static/shared runtime ABIs. Initial fixture execution exposed missing
context on fresh enum constructors and empty child literals; the lowering fix
must propagate the checked destination type before emitting those children.

The capability catalog now declares 21 groups and 43 proof fixtures (258 route
observations); installed consumers include 90 fixtures. These are test
registrations until fresh execution evidence is recorded below. Earlier SDKs
and the active SDK remain unchanged. Mainline consolidation remains last.

The runtime/storage ABI was checkpointed and pushed as `a746baa`; its two
positive static/shared suites and 34 separate failure-mode invocations pass.
The first integrated compiler run retains 18 passing focused tests and three
failures exposing source context gaps. Both Matrix fixtures pass all six routes;
24 separate cross-frontend imported generic-enum/nested-array executions pass,
including binary-only providers and qualified/unqualified source names.

Review also caught a concrete child-array width being rewritten when its outer
literal was partially inferred. The lowering guard now preserves that child's
ABI, while the checker supplies context to legitimate empty-first nested
literals. Enum payload checking applies ordinary class-view conversion before
lowering. Native array metadata requires exact enum schema names, and finite
schema validation includes concrete descendants of class views so an ancestor
payload cannot hide a recursive derived-field/enum graph. Focused regression
controls accompany each repair; broader execution results follow separately.

The storage tests additionally exposed lexical method dispatch when a local
shares its type's name (`queue`), tuple/generic payload commas being mistaken
for multiple enum fields, and inconsistent primitive alias normalization in
nongeneric enum schemas. These are repaired at their source boundaries. Flat
multi-payload patterns need distinct per-field types and exact arity; their
lowering is extended only for variable/wildcard payloads, with structured nested
patterns retaining their existing explicit rejection. Enum payload graphs use
the established unique nominal resolution internally, while an array's own
metadata still requires its exact declared element name.

The completed focused run passes all four Ny/Matrix fixtures across six routes,
the 320-check enum/nested-array contract, native NIL validation, generic enum
construction and the retained structured-pattern rejection. Tests cover mixed
wildcard positions and expression matches as well as statement matches. A
fixture-only correction supplies the existing required final wildcard arm for
match expressions. Native multi-payload extraction now follows declaration
order, matching the interpreter and NIL provenance analysis; the prior native
raw-NIL oracle had recorded the opposite order and is corrected explicitly.

The implementation is ready for a clean committed-source full suite and a
separate development SDK candidate. These focused results do not replace that
qualification or establish a new cross-platform release claim.

The first full run at `514204f` completed 627 tests: 620 passed, four failed and
three skipped for the existing unavailable providers. Three failures traced to
legacy untyped arithmetic in generated foreach loops losing integer provenance
at the loop merge. The loader now preserves only the proven integer category,
without inventing an exact width; float/unknown indices and guessed-width boxing
remain rejected. The fourth failure was an old array-identity assertion for a
fabricated nested-array default, now replaced by the documented read failure.
All four affected tests, the dynamic-value unit controls and the surrounding
array/native admission contracts pass after these corrections. A new clean
full run is required; the earlier failed report remains intact.

## Verified enum/nested-array development candidate

The corrected clean revision `c6ac4febe1e8c7fdd02d332b842645f5212167b4`
passes 624 of 627 local CTests with zero failures. Only the existing PostgreSQL
client/provider and graphical-display dependencies are skipped. Evidence is
retained under `enum-nested-arrays-20261126-u89bptwg`, including the earlier
failed full run, the corrected full report and the 320-check focused contract.

Its separate development SDK validates 11 root-level tools, 19 binary standard
library modules, source STL, 76 HTML documentation pages and 279 sealed files.
The capability record is verified for 21 groups and 43 proof fixtures, covering
258 six-route observations. All 1,440 installed consumer executions pass:
90 fixtures against both source and binary libraries, each using six execution
routes and text/binary NIL through the standalone assembler/linker. Installed
durable-I/O, console and global-contract checks also pass.

Bounded compatibility passes four full Web test-source compilations, four pure
Web suites with 393 assertions each, and eight synthetic WebView routes. Input
SDK, repository and provider pins remain unchanged, and cleanup is verified.
These checks start no server or desktop/browser UI. The SDK remains a separate
candidate pinned to `c6ac4fe`; it is not activated or declared a stable release.
Mainline consolidation and broader platform SDK qualification remain separate.

[GitHub Source validation](https://github.com/exec-software/exec-nil-framework/actions/runs/45942067075)
passes for exact `c6ac4fe`: LLVM enabled passes all 627 tests with zero skips;
LLVM disabled passes 475 with 145 expected native-only skips and no failures;
VS Code passes. This subsequent evidence-only notebook commit does not rebuild
or relabel the tested SDK candidate.

## Class/reference arrays and recursive schemas — 2026-11-27

The next storage slice adds exact class and aggregate-reference array kinds.
Classes retain checked ancestor views and virtual dispatch; `ref<Record>` and
`ref<Class>` preserve exact wrapper identity and shared object aliases. Existing
containers and reference wrappers remain invariant. The runtime addition was
committed separately as `5f8a935` after static/shared ABI suites and 38 explicit
rejection checks passed.

Native recursive schemas are finite graphs of handles, so rejecting every
schema back edge was unnecessary. Reachable schemas are now visited once with
the existing bounded validation and supported-layout checks. Empty arrays and
terminating enum variants permit initialized construction before back-references
form object cycles. Required-field escape checks remain; no default recursive
object is synthesized. Native GC already traces cycles. Direct cyclic aggregate
checkpoint restrictions and the interpreter's direct-value validation depth
limit remain separate contracts.

The first integrated checks exposed generic record methods whose `ref<Self>`
receiver had been lowered as a plain object. Explicit receiver-reference lowering
now preserves exact provenance and evaluates call/field receivers once. Raw
record-only reference arrays also needed `REF_LOCAL` to activate provenance
analysis; no class declaration is required to enforce wrapper identity.

The corrected focused run passes all 33 CTests, including four paired Ny/Matrix
fixtures across all six routes, the 174-check class/reference contract, the
49-check recursive contract and the retained 320-check enum/nested contract.
The regression runs and failed reports are preserved under
`class-recursive-arrays-20261127-7ah5wa2y`. No SDK was activated or relabeled.
Full qualification follows after the separate `f32` scalar/storage work; this
focused result does not declare a release or a new platform campaign.
