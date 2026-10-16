# Ny v1 consumer findings — 2026-10-03

This baseline uses the copied, development-qualified `1.0.0-rc.1` SDK identified
by `sdk-origin.json`. The compiler checkout and SDK payload were not changed
to make this project pass. A failure remains a failure in the report.

The expanded project contains three Ny rendering workloads and focused pointer,
binary, array, STL, diagnostic, and debugger cases. `baseline.json` preserves the latest complete
matrix, SDK identity, evidence hashes, and project source hashes. The initial
50-case measurement (305 PASS, 16 FAIL, 79 scoped SKIP) remains in
`baselines/phase-01.json` and `reports/phase-01/`.

The earlier informal model feedback supplied investigation leads, not findings.
Every issue below comes from this copied SDK and a retained reproduction.

The completed expanded matrix has **103 cases across eight routes: 643 PASS,
92 FAIL, and 89 scoped SKIP** (824 slots). Of the failures, 68 map to the five
issues below and 24 expose six native capability gaps. All three workloads pass all
eight routes with byte-identical output and zero reference-pixel differences.
All 185 SDK payload hashes remain unchanged; twelve binary fixture outputs
and eight scene checkpoints also match independently expected bytes.
This is a completed failing baseline,
not release qualification.

The standard-library expansion adds 15 probes exercising text, encoding,
binary, NyData, conversion, compiler and config surfaces. They are documented
in `probes/stdlib/README.md` and `probes/stdlib/findings.json`, and add two
confirmed correctness defects (`STL-007`, `STL-008`) plus four native capability
gaps. These are the newest findings and are described in the confirmed-defects
and native-gap sections below.

Phase 3 adds the persistent scene player and debugger testing. The failure set
is unchanged from phase 2; neither the compiler nor copied SDK was patched.
`baselines/phase-02.json` and `reports/phase-02/` retain the previous evidence
and a hash-verified consumer-source snapshot.

## Persistent frames and binary checkpoint

The static mesh code now lives in a shared local module; its output still
matches the pre-refactor renderer exactly. The new scene player builds once,
copies base coordinates explicitly, and reuses four raw buffers while updating
the scene. Each frame resets the pixel/depth buffers and records hashes and
fragment counts. Final geometry and telemetry are written atomically and read
back through the copied STL's checked binary APIs.

The larger run advances **120 frames at 256×192 inside each process**, on both
the interpreter and native AOT routes. All frame digests/fragments match the
independent reference, all 49,152 final pixels match exactly, and the complete
5,240-byte checkpoint matches independently packed bytes and CRC32. The final
digest is `2501747411`, with 13,170 depth-passing fragments. The default
12-frame workload also passes all eight routes.

Additional controls cover repeated invocations, argument limits, a directory
occupying the checkpoint path, and corrupt reference inputs. A deliberately
non-writing repeat is rejected: the harness seeds an invalid output file before
every process, with a different sentinel for each repeat. These are application/harness checks, not newly
found Ny defects. Intermediate images are checked via 32-bit digest and fragment
count; only the final frame has a complete pixel comparison. This exercises
persistent state but is not a heap/leak measurement or a checkpoint-resume test.
See `SCENE_PIPELINE.md`, `reports/scene-stress.json`, and `reports/scene-repeat.json`.

## Debugger baseline and inspection gaps

The debugger audit passes **122 checks** across text and binary debug NIL,
including breakpoints, scalar conditions, step in/over/out, source stacks,
named locals, lexical shadowing, evaluation, watches, stale-reference rejection,
JSONL restart, and cooperative pause/termination. It debugs the actual mesh
renderer and persistent frame loop, crosses the imported module boundary, then
compares output and checkpoint bytes against normal interpreter execution.
The count includes compiler, integrity, and documented-rejection checks.

No debugger execution defect was reproduced. Two concrete inspection proposals
are recorded separately:

- **NYDBG-001:** JSONL `stopped.file` names the NIL artifact while its line is a
  source line. The stack frame contains the correct source path. Clarify the
  schema or supply an explicit source-file field; clients should currently use
  the stack frame's file/line pair.
- **NYDBG-002:** DAP array expansion exposes an internal handle instead of
  elements, and raw pointers appear as untyped integers. Mesh struct members
  expand, but inspecting their array data remains difficult. Add lengths,
  paged elements, declared pointer types, and bounded memory views.

The installed documentation does not promise these visualizers or define the
JSONL field contract precisely, so they are usability findings rather than
failed supported-feature tests. Debugging is NIL-only: native attach/DWARF is
documented unavailable. Native execution is tested independently. Editor UI,
worker-thread stepping, blocking-I/O interruption, and exhaustive debugger
behavior remain outside this audit. See `probes/debugger/README.md`,
`reports/debugger.json`, and retained protocol transcripts.

## Confirmed defects

| ID | Reproduction | Expected | Observed |
| --- | --- | --- | --- |
| NYVAL-001 | `probes/i64-minimum-literal.ny` | Print the representable signed minimum `-9223372036854775808` | All eight routes fail. Frontend output contains the out-of-range positive operand `9223372036854775808`; NIL loading or native lowering rejects it. |
| NYVAL-002 | `probes/documented-module-alias.ny` | Resolve the namespace alias syntax shown in the SDK modules documentation | All eight routes report the alias `scene` undefined. Selective imports and `::` qualification pass in a separate probe. |
| STL-001 | `probes/stl/stl-{list,queue,stack}-copy-overflow.ny` | Reject copying two items into a two-element destination at offset 2147483647 | The i32 `offset + count` guard overflows; all three collections return successfully. Ordinary offset 1 correctly rejects in paired controls. |
| STL-007 | `probes/stdlib/stl-convert-i64-text.ny` | `Convert.i64_to_text(-9000000000)` prints `-9000000000` | The formatter narrows i64 to i32, printing `-410065408`; the i64 minimum prints `0`. i32 controls and positive i64 values pass, isolating negative magnitudes. |
| STL-008 | `probes/stdlib/stl-nested-call-clobber.ny`, `stl-config-parser.ny` | `trim(substring("fov: 60.5", 0, 3))` returns `"fov"` | A nested call with live locals clobbers the substring length, returning `"o"`. The same clobber corrupts 3-char config keys, so `fov` lookups return the fallback. Longer keys survive. |
| NYDIAG-001 | `probes/diagnostics/wrong-generic-arity.ny`, `unknown-extra-generic-type.ny` | Reject excess explicit type arguments, including an undefined extra type | A function declaring one type parameter accepts `identity<i32,f64>(3)` and `identity<i32,NoSuchType>(3)` and prints 3. |
| NYDIAG-002 | `probes/diagnostics/wrong-function-argument-type.ny`, `wrong-explicit-generic-type.ny` | Diagnose an integer argument supplied to a `text` parameter during frontend checking | Both ordinary and explicitly specialized generic calls survive frontend checking and print 3 interpreted. Native routes reject later in code generation. String-argument controls pass. |

Reproduce the original defects:

```bash
python3 scripts/validate.py \
  --only i64-minimum-literal,documented-module-alias \
  --report reports/known-defects.json
```

The first issue needs a literal/lowering consistency fix, with the direct
literal retained as a test. Computing the same minimum through subtraction
already passes the storage, bitwise and serialization probes. The second needs
the advertised alias behavior implemented, or the public syntax and diagnostic
contract corrected before release. The project preserves both reproductions.

Reproduce the newly confirmed defects:

```bash
python3 scripts/validate.py --only \
stl-list-copy-overflow,stl-queue-copy-overflow,stl-stack-copy-overflow,diag-wrong-generic-arity,diag-unknown-extra-generic-type,diag-wrong-function-argument-type,diag-wrong-explicit-generic-type \
  --report reports/new-defects.json
```

For `copy_to`, first check the offset against the destination length, then
compare count with `length - offset`; reject before writing. The copied
dictionary and hash-set implementations contain the same guard by inspection,
but this defect was executed only for list, queue, and stack. Current array
write semantics make these particular out-of-range writes no-ops; the failed
contract is silent acceptance, not demonstrated memory corruption.

Reproduce the standard-library defects:

```bash
python3 scripts/validate.py --only \
stl-convert-i64-text,stl-nested-call-clobber,stl-config-parser \
  --report reports/stdlib-defects.json
```

For `STL-007`, format i64 with a 64-bit divisor/remainder loop instead of
narrowing through i32, and add negative-magnitude regression cases. For
`STL-008`, preserve the inner substring length across nested calls with live
locals. `stl-config-parser` additionally fails native aggregate array lowering,
which is a separate native gap from the clobber.

Validate explicit generic arity, resolve every supplied type argument, and
check call arguments against substituted parameter types before producing NIL.
For NYDIAG-002, JIT/AOT count as successful rejection because their combined
compiler command fails; assembler routes still fail the fixture because
source-to-NIL compilation accepts the bad call and rejection occurs later.
Diagnostic-stage evidence is the useful distinction, not the PASS label alone.
The copied documentation does not comprehensively specify free-function
generic arity; the recorded evidence is a one-parameter declaration accepting
two arguments, including an undefined type. These cases stay visible instead
of being converted into expected successes.

## Diagnostic quality

The dedicated audit separates correct rejection from useful error reporting.
It pairs 13 problematic/boundary sources with valid controls on NIL, JIT, and
AOT. All 39 controls pass. The 78 checks produce 70 PASS and 8 FAIL; those
failures are the accepted calls described above. The audit records the actual
stage and complete stderr, so late native rejection does not conceal missing
frontend checking.

Across 27 emitted diagnostics, 18 mention a line, 3 mention the source filename,
and none provide a column or source excerpt. The filename examples are parser
errors; most type errors report a line alone. The tested native layout and
runtime shift errors lack a source location. Messages usually identify the
construct and rule, but no measured case offers a repair hint. These are
observations of the fixtures, not a claim about every Ny diagnostic.

Recommended improvements: carry filename/line/column through parsing, checking,
NIL and native execution; display the offending source span; preserve expected
and actual types; and explain unsupported collections at the user's call site
instead of only naming internal layouts or temporary callees. Missing imports
and misspelled identifiers are useful candidates for targeted hints. See
`probes/diagnostics/README.md` and `reports/diagnostics.json` for exact messages,
controls, stage distinctions, and measurement limits.

## Pointer safety decision

The checked-mode raw-pointer probes observe these behaviors across interpreter
and native routes:

| Operation | Observed result |
| --- | --- |
| Read beyond the buffer, at a negative index, or through a freed alias | Zero |
| Write beyond the buffer | No effect |
| Double free or interior-pointer free | Returns successfully; interpreter warns, native is silent |

These are **characterization results, not approval of a v1 safety policy**.
The copied documentation does not promise fatal traps here. Decide whether
checked code should trap, return an explicit error, or keep these semantics;
then align the documentation, diagnostics and tests across routes. Silent zero
or ignored writes can conceal mistakes in a larger 3D application.

Zero/negative allocation counts intentionally request one element according
to the documented contract, and pass that expectation. The suite never uses
`--unmanaged`.

## Documented capability limits

- Native `ptr<f64>` is excluded. This renderer uses `f64` structs for geometry
  and `ptr<i64>` fixed-point records for scene storage, plus `ptr<i32>` pixels
  and `ptr<u8>` channels. This does not establish arbitrary pointer layouts.
- Compatibility `write_bytes` works interpreted but is rejected by the native
  backend. **Native binary file output is available** through the supported
  `write_bytes_atomic` and `read_regular_bytes` APIs; the project checks their
  exact bytes, read limit and no-clobber behavior. The renderer emits PPM to
  stdout for easy cross-route comparison.
- Pointer-to-pointer subtraction, reinterpretation, scalar stack references,
  and invalid ownership copies/loans are tested rejection boundaries.
- Native arrays support six scalar element kinds; aggregate and nested arrays
  are excluded. Scalar `map<text,i32>` and `dictionary<text,i32>` still fail
  native lowering because their internal entries use aggregate arrays. Text
  sorting also fails native comparison lowering. Callback-based `vec.map` and
  `filter` hit incomplete native indirect-call support. These are six active
  consumer cases with successful interpreted behavior and visible native
  failures, separate from the correctness defects above.

## STL expansion notes

The 24 STL cases exercise array growth and aliases, all six scalar element
kinds, vec/list/queue/stack operations, material maps, sorting/searching, empty
algorithms, f64 depth keys, and copy bounds. Concrete expansion requests are:

1. Add checked array access returning Option/Result, or debug checks that name
   the index and length. Existing zero/no-op access is documented, but a bad
   mesh index can silently become a valid-looking coordinate zero.
2. Specify real reserve/resize/fill, append-range, and overlap-safe bulk copy.
   Current `vec.with_capacity` is explicitly advisory; its observed capacity
   of zero is not a broken reservation guarantee.
3. Add bounded views with length, offset, stride, and explicit ownership rules
   for image rows, vertex attributes, and binary chunks.
4. Complete and publish native support per collection and element type,
   especially aggregate storage, lexical text comparisons, and callbacks.
5. Consider ring queues, actual hash maps, and stable index/permutation sorts.
   Installed queues shift on dequeue and maps scan entries linearly; this is
   a source-based complexity observation, not a measured performance claim.

`probes/stl/README.md` and `probes/stl/findings.json` distinguish defects,
documented limits, usability observations, and proposed APIs. Explicit i64
array literal casts and 0/1 boolean output corrected early test fixtures;
those fixture mistakes are not Ny findings.

## Positive evidence

- A four-sphere 3D scene with checker floor, shadows, reflections and fog runs
  through all eight execution routes. Default 128×96 output is byte-identical.
- A separate 512×384 workload completes twice on both interpreter and AOT
  routes. All 196,608 pixels and the row hashes match an independent Python
  geometry/shading/packing implementation exactly, with zero channel error.
- The indexed terrain/tower renderer builds 313 vertices and 548 triangles,
  using scalar arrays plus raw RGBA, depth, RGB, and checksum buffers. Its
  separate 512×384 run completes twice on interpreter and AOT; all pixels,
  row hashes, mesh counts, and 52,167 depth-passing fragments exactly match
  the independent integer reference. Twelve further image checks cover tiny,
  non-square, default, and extreme aspect-ratio outputs; corrupting pixels,
  row hashes, fragment counts, or mesh counts causes the oracle to fail.
- The pointer/bitwise soak performs 12 allocation/free cycles over 2,048 records
  each, writes 73,728 scalar slots and produces the independently calculated
  digest `6848284069084606464`.
- Other probes exercise integer-width and shift boundaries, packed records,
  endian conversion, varint/ZigZag, CRC32, structs/generics/references, class
  dispatch, faults and cleanup, owned buffers, parallel workers and modules.
- The SDK manifest and all 185 payload hashes are checked before and after
  runs. A Linux `strace` audit of NIL/native compile-and-run invocations found
  no file access to the original compiler checkout.

`reports/validation.json`, `reports/scene-stress.json`, `reports/debugger.json`,
the preserved static-render stress reports, `reports/diagnostics.json`, and `reports/isolation.json`
contain the measured commands and results. The isolation audit covers those
invocations; it is not a filesystem sandbox. Timings are local observations,
not a controlled performance benchmark.

## Next validation areas

Prioritize the confirmed bounds/type-checking defects and the original
literal/module issues, then choose the pointer and array safety contract.
After those decisions,
extend this same consumer project to checkpoint resume and malformed asset
loading, compiled-library imports, a native window/event loop and graphics
provider, editor/LSP workflows, and foreign
library integration. Network/database providers and other operating systems
need separate fixtures. Passing this baseline is not exhaustive v1
qualification.
