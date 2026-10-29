# Matrix submissions and pure sessions

The compiler checks and evaluates Matrix statements followed by an optional
final expression, without a book or paragraph wrapper. The interactive shell
retains immutable, owned values between submissions. Matrix source and strict
NILData requests share the parser/type-checker/profile pipeline described in
[NIL-0018](../design/NIL-0018-matrix-shell-profile.md).

```sh
printf 'value amount is 40\namount plus 2\n' | nilcompiler --check-submission -
printf 'value amount is 40\namount plus 2\n' | nilcompiler --evaluate-submission -
nilcompiler --check-submission-request request.nildata
nilcompiler --evaluate-submission-request request.nildata
nilcompiler --matrix-shell
```

The check and evaluate commands read one bounded regular file or standard input
through EOF, then write one JSON report. Each evaluation command creates a fresh
session. Use `--matrix-shell` or the C++ `matrix::submission_session` API to keep
bindings across submissions. Every command must be used alone: normal compiler
switches cannot disable checking, add imports, select native execution, or write
an output artifact.

## Check-only reports

`--check-submission <path|->` and `--check-submission-request <path|->` retain
their existing inspection behavior. They parse, check the AST profile, run the
ordinary type checker, generate NIL and verify that artifact. They do not
execute or retain bindings.

Their report schema is `nil.matrix.submission-check.v1`, with admission profile
`matrix.pure.v1`. Reports include `input_kind`, `status`, `executed: false`,
`result_type`, `nil`, `instruction_count` and `diagnostics`. Only a complete,
admitted submission has a result type and generated NIL. `void` means there is
no final expression. The `nil` field is inspection text; it is neither an
execution authorization nor a serialized session.

## Evaluation reports and values

`--evaluate-submission <path|->` and
`--evaluate-submission-request <path|->` use schema
`nil.matrix.submission-evaluation.v1`. Reports retain the check fields except
`nil`, and add `committed`, `value`, `added`, `binding_count` and
`instructions_executed`. `executed` records whether VM instructions ran;
`committed` records whether the entire transaction passed publication checks.
A failed transaction returns no value or newly added bindings. A successful
expression-only or empty submission can commit without adding a binding.

Values have explicit types. Every scalar payload is a JSON **string**, including
integers, finite floats and booleans. This preserves the full `i64` range when a
client uses JSON numbers with limited precision. Collection values carry an
`elements` array of recursively typed values. For example, the `value` field for
`(9223372036854775807, true)` is:

```json
{
  "type": "tuple<i64,bool>",
  "elements": [
    {"type": "i64", "scalar": "9223372036854775807"},
    {"type": "bool", "scalar": "true"}
  ]
}
```

Integer payloads use canonical decimal text. Float payloads use round-trip
precision with a decimal point or exponent; negative zero is preserved.
Nonfinite floats cannot be published. Text remains a string, with controls
escaped by JSON serialization. A `void` result has `value: null`; each entry in
`added` is `{ "name": ..., "value": ... }` using the same typed representation.

| Status | Single-evaluation exit code | Meaning |
| --- | --- | --- |
| `complete` | 0 | Checking, execution and publication succeeded; for check-only commands, both admission gates passed. |
| `incomplete` | 2 | Matrix syntax needs more input, such as an operand or closing block. |
| `invalid` | 1 | Input, parsing, schema, type, session-name or profile checks failed. |
| `runtime-error` | 1 | Admitted computation or typed result export failed. |
| `budget-exceeded` | 1 | Execution, state, restoration or report limits prevented publication. |
| `cancelled` | 1 | The embedding host requested cooperative cancellation. |

Malformed command options produce a command-line error rather than a submission
report. NILData requests must be complete documents; an unfinished request is
invalid. Diagnostics carry stage, code, message, location, line, column and
length. Parser diagnostics preserve original Matrix positions; later stages may
provide only a line. Structured expression errors use paths such as `$.value`
or `$.bindings[0].value`. Zero positions mean unavailable. The runtime identifies
the executing instruction's source line when available.

## Interactive use

A complete line evaluates immediately. Incomplete Matrix input accumulates until
it forms a complete submission. Inside `begin text`, command-like lines remain
literal payload; close the block with `end text` before using a shell command. To make several otherwise complete lines one
transaction, use an explicit block:

```text
:begin
value Frame-Count as number is 40
value next-count is frame_count plus 2
next-count
:submit
FRAMECOUNT
:bindings
:quit
```

The first submission publishes both bindings and returns `42`. The next
expression reads the retained `Frame_Count` as `i64`. Matrix case/separator
folding applies across submissions; redeclaring an existing folded name fails.
Session binding names must be unescaped Matrix identifiers. Nested block locals
are not retained, and `_` in tuple destructuring does not create a binding.

| Command | Behavior |
| --- | --- |
| `:begin` | Start an atomic input block; defer evaluation until `:submit`. |
| `:submit` | Evaluate pending input; incomplete input stays pending. |
| `:cancel` | Discard pending input without changing retained values. |
| `:bindings` | List retained names and typed values; piped output uses `nil.matrix.session-bindings.v1`. |
| `:reset` | Clear retained bindings and pending input. |
| `:quit` | Exit and discard pending input. |
| `:help` | Show commands; piped input receives `nil.matrix.shell-help.v1` JSON. |

When both input and output are terminals, the shell shows prompts and readable
typed results: `42 : i32`, or `count = 42 : i32` for a binding. Text is quoted and
escaped, arrays use `[1, 2]`, and tuples use `(1, true)`; a one-element tuple
retains its comma. Empty success prints `ok`, and an empty `:bindings` prints
`No retained bindings.` Diagnostics show their status, message and available
source position. Terminal controls and directional characters are escaped.

With piped input or output there are no prompts; results, binding inspection and
help use JSON records. Single-evaluation commands always return JSON, including
when run in a terminal. A failed shell submission does not stop later input.
The piped shell exits 1 if any input failed, otherwise 2 if EOF leaves incomplete
input or an unsubmitted atomic block, otherwise 0. Interactive `:quit` exits 0.
An oversized line or accumulated submission enters discard mode. Input resumes
only after `:submit`, `:cancel` or `:reset` (or exits with `:quit`), so the
remaining lines of a rejected submission cannot run separately.

`:cancel` manages input collection; it is not an asynchronous interrupt for a
running evaluation. Embedding hosts can supply a cancellation callback, checked
between VM instructions, during value export and before publication. The shell
stores no persistent source history, result log or session file. There is no
command expansion, process launch, environment expansion or host filesystem API
inside the language profile.

## Ownership and publication

Every evaluation uses a fresh shared VM. Retained values are owned scalar/text,
array and tuple snapshots with exact types, not VM addresses or mutable aliases.
The compiler restores those snapshots as typed literal AST data, then compiles
the new submission. It never reruns earlier expressions or reconstructs source
text from values. Both source and request calls on the same
`matrix::submission_session` instance see those bindings.

New top-level bindings are published together only after execution, typed
snapshot export, cumulative state limits, compiler restoration checks and report
projection succeed. Parse/type/profile failures, runtime errors, nonfinite
results, exhausted budgets and cancellation leave prior bindings unchanged.
An embedding host owns one session on one thread; the API does not provide
concurrent access or persistence.

## Admitted computation

Profile `matrix.pure.v1` admits immutable `value` bindings, supported scalar/text
expressions, tuple construction/destructuring, concrete arrays and indexing,
numeric casts, formatted text and conditionals. A single bare expression may
appear at the end. Use a value position or parentheses for a value-producing
conditional when its syntax would otherwise select a statement.

Calls (including library functions), imports, named declarations, annotations,
mutation, loops, collected queries, pointer/reference operations and assembly
are rejected, including in branches that would not run. The generated NIL gate
independently checks all instructions, type metadata, stack/local initialization,
fresh tuple construction and forward control flow. The VM revalidates its loaded
program and rejects forbidden operations before execution. No host catalog or
Anomalous adapter is installed. Evaluation uses the hosted shared VM; there is
no native submission evaluator.

These exclusions apply to the shell profile, not ordinary Matrix books. The
`shell-profile-preview.matrix` sample is a full program using calls, queries and
output, so it is not an admitted pure submission. Admission does not prove
runtime success: integer division by zero can pass checking. Array indexing
currently follows the shared language's existing behavior: an out-of-range
scalar/text array read returns a default value, while a tuple-array read fails.
This inconsistency is recorded as [F73](../../matrix/docs/next-version-findings.md#f73--array-indexing-needs-one-checked-failure-contract).

## Structured request schema

A NILData request constructs the same shared AST directly. It cannot embed
Matrix source, serialized NIL, arbitrary compiler AST fields or authority claims.
Every object has an exact set of required fields; unknown fields, versions and
node kinds are errors.

```nildata
nildata 1
object
  schema is "nil.matrix.submission-request.v1"
  profile is "matrix.pure.v1"
  bindings is an array
    item is an object
      name is "amount"
      value is an object
        kind is "number"
        value is "40"
      end object
    end object
  end array
  value is an object
    kind is "binary"
    operator is "plus"
    left is an object
      kind is "name"
      name is "amount"
    end object
    right is an object
      kind is "number"
      value is "2"
    end object
  end object
end object
```

The four envelope fields are `schema`, `profile`, `bindings` and `value`.
Bindings are ordered `{name, value}` objects; each value is an expression.
The final `value` is an expression or null for a void submission. Names use
one unescaped Matrix identifier, with Matrix folding and duplicate-name rules.
References must resolve to an earlier binding in the request or an existing
binding supplied by the session API. They cannot name host operations.

Each expression has `kind` and the following additional required fields:

| Kind | Fields |
| --- | --- |
| `number` | `value`: exact numeric token as text, such as `"-1.6"`; checked by the shared numeric rules |
| `text` | `value`: text |
| `boolean` | `value`: boolean |
| `name` | `name`: binding name |
| `unary` | `operator`, `operand` |
| `binary` | `operator`, `left`, `right` |
| `tuple`, `array` | `items`: expression array |
| `index` | `target`, `index`: expressions |
| `member` | `target`: expression; `index`: canonical nonnegative integer text for a tuple position |
| `cast` | `value`: expression; `type`: `bool`, `i32`, `i64`, `u8` or `f64` |
| `conditional` | `condition`, `then`, `otherwise`: expressions |

Unary operators are `negative`, `not` and `bitwise-not`. Binary operators are
`plus`, `minus`, `times`, `divided-by`, `modulo`, `equals`, `not-equals`,
`less-than`, `less-or-equal`, `greater-than`, `greater-or-equal`, `and`, `or`,
`bitwise-and`, `bitwise-or`, `bitwise-xor`, `shift-left` and `shift-right`.
Type/profile validation still applies to decoded expressions.

## Bounds and verification

Inputs remain limited to 65,536 bytes of valid UTF-8 with no embedded NUL.
Matrix scanning allows 4,096 tokens and parsing allows 64 nesting levels.
Requests also use the shared NILData limits and a 32-level expression decoder
limit. AST admission allows at most 4,096 nodes and 64 levels, with additional
name, collection and type-shape limits. Generated NIL allows 4,096 instructions,
256 locals/stack slots and 1 MiB of tracked text. Retained data restoration counts
against these compiler limits too.

The hosted defaults are:

| Resource | Default |
| --- | --- |
| Executed VM instructions | 16,384 |
| Charged VM work | 16 MiB |
| Individual text / exported snapshot bytes | 1 MiB each |
| Exported snapshot nodes / depth | 4,096 / 32 |
| Retained session bindings / value nodes | 64 / 1,024 |
| Retained state / successful result report | 1 MiB each |
| Execution and publication deadline | 1 second |

The execution deadline begins after initial compilation and program loading.
Checks run between instructions and at publication checkpoints, including after
snapshot/restoration work and report projection. These are cooperative logical
limits, not process RSS accounting, hard preemption or a general OS sandbox.
Initial parsing/checking uses its structural limits rather than this deadline.
Diagnostic reports can exceed a host's deliberately tiny successful-report
limit. Hosts may configure supported limits through the C++ API; these are not
CLI switches.

The admission suites remain `matrix.submission-parser`,
`matrix.submission-request`, `nil.submission-profile` and
`matrix.submission-contract`. Runtime regression suites are
`nil.submission-vm`, `matrix.session-compile`, `matrix.session` and
`matrix.session-contract`, covering typed restoration, source/request binding
lookup, rollback, cancellation, resource accounting, option isolation and shell
input boundaries. Their presence defines the checks to run; it does not qualify
a native Anomalous runtime or a host authority adapter.
