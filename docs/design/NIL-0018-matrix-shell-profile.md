# NIL-0018: A Matrix profile for interactive shell work

Status: proposed; the companion example runs as an ordinary hosted Matrix
program. No shell command, interactive evaluator, restricted VM, or Anomalous
execution route is implemented by this record.

## Purpose and existing direction

Use Matrix for interactive work and scripts so that learning the shell also
teaches the language used to build Anomalous applications. Keep its named calls,
immutable values, explicit blocks, checked results and typed queries. Reuse the
Matrix lexer/parser, shared type checker, NIL representation and language tools.

Anomalous already proposes **Canticle** as a shell/session environment in its
decision record 0010 and `docs/architecture/canticle.md`. Its syntax is still
proposed. The recommendation is to make Matrix the language surface of that environment while
preserving its shared human/machine invocation model, explicit authority,
structured results and preview/commit requirements. This framework proposal
does not amend or accept an Anomalous decision record.

Anomalous decision record 0092 selects an unprivileged native interpreter vessel.
Its decision record 0093 requires artifact-derived authority bounded by that
interpreter's ceiling.
Running the current hosted C++ VM on Linux does not establish either property.
The newer `ANPG` startup descriptor and program arena are scaffolding: the
current native program profile explicitly supplies no NIL execution, edition
mapping, or console transport.

## What a person would write

The following is a **target interactive example**, not runnable shell syntax
today. Command names and their schemas are illustrative. The session supplies
the named object and authority bindings; writing their names cannot create them.

```matrix
value response is list-files with location being workspace, authority being workspace_read
consider response
    when Result.Ok holding entries then
        value sources is collect begin query
            from each entry in entries
            where entry.extension equals ".mat"
            order by entry.name
            take 20
            give entry
        end query
        evaluate present with sources
    when Result.Error holding failure then
        evaluate present-error with failure
end consider
```

The proposed local `present` adapter projects the typed value as a table. A
machine receives the same value with its schema, without reading terminal cells.
`present-error` renders only the permitted error fields with source, operation
and trace information; failure does not produce an empty success table. These
adapters are proposed session APIs, not existing standard-library functions.
Their projection is local; persistence or disclosure to another principal is
an explicit operation. Automatic checked-result propagation would require a
separate, shared language decision and must not silently call `unwrap`.

Current queries cannot use the record rows shown here: the shared query profile
admits scalar/text and supported tuple elements. Named record rows are a real
language/runtime prerequisite for this interface, not a new shell-only table
type. The [executable syntax proof](../../matrix/samples/shell-profile-preview.matrix)
uses tuples and explicit `Result` handling under the existing contract.

The prompt should accept a submission containing statements and a final
expression. Saved programs retain ordinary `.mat`/`.matrix` pages. Exporting a
session creates a normal book/paragraph with explicit inputs, rather than a
different script grammar. The current parser requires those declarations;
submission parsing and final-value return are work to implement.

## Initial profile

| Area | First hosted prototype | Follow-up |
| --- | --- | --- |
| Values | Existing scalar/text, supported tuples and arrays; `Option`/`Result` | Named record arrays and checked domain values |
| Computation | Immutable bindings, arithmetic, comparison, explicit formatted text, conditionals, collected queries | User paragraphs and bounded iteration after transitive validation |
| Commands | Typed, versioned mock `help` and system observations | Real observation adapters admitted by Anomalous |
| Imports | A fixed, explicitly admitted module catalog | Versioned user modules after effect/operation verification |
| Session | Explicit inputs and outputs; new code never replays prior effects | Retained bindings with managed ownership and deterministic cleanup |
| Effects | No live external operations in the syntax proof | Observation, then preview/commit, cancellation and recovery |

The execution profile excludes raw memory, assembly, arbitrary foreign calls,
user intrinsic declarations, general recursion, unbounded loops, unrestricted
parallelism and dynamic evaluation. The full Matrix language retains its systems
features; this is an admitted workload profile with its own limits.

`value` prevents rebinding but does not make an array deeply immutable. Session
publication must account for aliases: a failed submission must not leave earlier
bindings changed through a shared array. Admit read-only operations first, then
define ownership/copy rules before permitting updates to retained values.

Do not introduce command search through `PATH`, implicit environment expansion,
word splitting, globbing, command substitution or execution of unknown names.
Formatted text remains text. Foreign programs eventually use an explicit
operation with an executable identity and an array of arguments.

## Shared execution and authority

```text
Matrix submission -> Matrix parser -> shared AST/type checker -> typed NIL
structured request -> checked schema/resolver ----------------------^
                                                              profile verifier
                                                                    |
                                                              bounded executor
                                                                    |
                                                         typed host command adapter
                                                                    |
                                                  Anomalous invocation/policy boundary
```

Both entry paths must produce equivalent resolved operations and pass the same
limits. A structured request cannot assert that it is already checked or trusted.
Use a small schema for the admitted profile, rather than accepting arbitrary
internal compiler AST objects. Retain source spans or request-node identities
for equivalent diagnostic attribution.

The host catalog binds each command to an exact operation identity, schema
version, argument slots, result type and effect summary. Generated declarations
allow ordinary Matrix named-call checking and completion. Runtime dispatch must
reject an unregistered command; it must not fall through to native-library
loading. Default-deny checks must cover transitive calls, indirect calls,
initializers, imports and serialized NIL, including paths that do not execute.
The existing worker verifier is a starting point, not a general authority proof.

Authority references are opaque host-owned bindings, not public records,
integers, NILData objects or bearer tokens in source. Resolving a name or passing
type checking confers no authority. Admission bounds the program, while each
invocation still validates its current grant, principal, target and revocation
state. A compile-time family allowlist cannot replace that check.

Pure evaluation may construct a request; it must not discover its effects by
performing them. Material operations require a resolved preview and a later
commit bound to exact targets, versions, policy and an idempotency identity.
Stale plans fail explicitly. Cancellation, unknown completion, partial effects
and compensation remain distinct outcomes. These are later implementation gates,
not capabilities of the example.

## NILData and sessions

NILData can carry versioned request and result envelopes, examples and history
metadata. It is a data encoding, not an executor, authority token or AST schema.
Encoding executable node names does not authorize their evaluation. Unknown
versions and node kinds must be rejected by a separate decoder.

Do not use normalized NILData text as a cryptographic plan digest. NILData
preserves numeric spellings, and different spellings may have the same numeric
meaning. The bound-plan schema must define identity and canonical encoding
separately; it must also exclude capability material and respect disclosure
policy. Initial equality tests can compare resolved typed operations directly.

History retention is an explicit session policy. Source, arguments, completion
queries, result values and traces may all contain sensitive data. A session must
not persist everything merely because it was typed at the prompt. Projection
must escape terminal controls and preserve structured values; exporting or
sharing a result is a separate operation.

## Runtime gaps to close

- The Matrix parser has no submission entry point or incremental completeness
  result. Add it using existing expression/statement parsing and original source
  locations. Do not create a second shell parser by rewriting source strings.
- `VM::loadProgram` resets globals, heap and execution state. Recompiling and
  replaying the complete session would repeat effects. Define a retained binding
  contract and transactional publication of new bindings instead.
- The VM has host callbacks, injectable streams, exit trapping and instruction
  stepping, but no general sandbox/profile enforcement. External dispatch can
  reach native-library loading. An allowlisted host adapter needs a closed path.
- Instruction stepping alone does not bound allocations, parsing/type checking,
  output, native calls or the work performed inside one intrinsic. Give each
  phase explicit budgets, cancellation behavior and host resource containment.
- Existing `ny.context.v2` inspection reports authored intent. It is not the
  authority manifest required by Anomalous decision record 0093. Reserved NIL
  format fields must not be repurposed without their own version/compatibility decision.
- Anomalous's copied runtime and refresh script still use old Ny SDK layout
  assumptions. Reconcile that consumer with `exec-nil-framework`, `nilcompiler`,
  `nilinterpreter`, `nilstd` and `nillib` before adopting the new fixtures.

## Alternatives and task corpus

The existing Canticle draft is the contrasting candidate: its proposed
`invoke ... with [...]`, `|>` and explicit `preview`/`commit` forms offer useful
effect visibility, but require another grammar and toolchain. A Matrix profile
reuses language knowledge and tools; its command/effect boundary must remain
visible through typed catalog declarations and inspected operations.

| Task | Matrix direction | Canticle draft | Acceptance evidence needed |
| --- | --- | --- | --- |
| Inspect system state | Named observation call and checked result | `invoke` with authority bindings | Equal resolved operation and error on both inputs |
| Filter unhealthy vessels | Existing collected query; named record rows pending | Typed pipeline | Equal ordered rows; no text scraping |
| Preview a removal | Explicit typed preview/commit operations | Dedicated preview/commit forms | Exact plan identity; stale/denied commit does nothing |
| Pass hostile text to a program | Explicit argument array | Explicit foreign-program rite | One argument preserved; no parsing or expansion |
| Deadline on concurrent observations | Deferred structured operation | Deferred structured concurrency | Bounded fan-out, cancellation and partial-error attribution |
| Handle one failed item | Explicit `Result` matching | Result propagation and later matching | Successful rows retained only under an explicit partial-result contract |
| Use a secret once | Deferred opaque governed value | Proposed `secret<T>` | No ordinary formatting, history or serialization exposure |
| Human/machine equivalent request | Same resolver and profile verification | Same bound program | Differential source/request corpus and trace attribution |

This is an architectural comparison, not measured usability or security
evidence. The companion program demonstrates existing Matrix syntax and hosted
execution only. Actual submission, hostile-input, resource and authority tests
are required before selecting a deployable profile.

## Implementation sequence

1. Reconcile the copied SDK and document the Anomalous consumer baseline.
2. Add Matrix submission parsing and a pure, default-deny profile verifier.
   Exercise complete/incomplete/error input and text/request equivalence without
   a live executor or host effects.
3. Add a bounded hosted evaluator with typed mock observations, final-value
   rendering and explicit session binding ownership. Test no effect replay,
   forged handles, unknown operations, budget exhaustion and cancellation.
4. Generate command signatures for completion/help from the same versioned
   catalog used by checking and dispatch. Add native named record-row support
   through the shared language implementation.
5. Qualify Anomalous interpreter execution and its observation adapters under
   its existing ADRs; run the shared language corpus and profile rejection cases.
6. Add mutations only with verified preview, commit, freshness, revocation,
   idempotency and attributable failure behavior.

Source references reviewed: `matrix_parser.*`, `vm.*`,
`concurrency_contract.cpp`, the query and array contracts, Anomalous decision
records 0010/0092/0093/0094, Canticle architecture, `program_startup.rs`,
`native_program_profile.rs` and `refresh-ny-runtime.sh` (2026-10-26).

## Reproducing the syntax proof

From the framework repository root, with a qualified Linux SDK available:

```sh
python3 tests/routes/ny_route_matrix.py \
  --source matrix/samples/shell-profile-preview.matrix \
  --compiler /path/to/sdk/nilcompiler \
  --interpreter /path/to/sdk/nilinterpreter \
  --compiler-arg=--sdk-path --compiler-arg=/path/to/sdk/nilstd \
  --routes text-interpreter,binary-interpreter,llvm-aot \
  --expected-stdout matrix/samples/shell-profile-preview.stdout \
  --expected-exit 0 --work-dir build/matrix-shell-preview \
  --report-json build/matrix-shell-preview.json --strict
```

All three routes passed with identical expected output, exit zero and empty
stderr on 2026-10-26 using SDK source revision `9e33415`. The proof calls only
fixed in-memory fixture paragraphs. It includes a finite display loop and local
`say` statements as a normal program; this does not establish admission under
the proposed restricted shell profile.
