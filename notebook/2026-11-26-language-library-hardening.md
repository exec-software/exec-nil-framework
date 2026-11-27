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
