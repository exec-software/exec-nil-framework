# Matrix submission inspection

The compiler provides a check-only entry point for the first Matrix shell
profile. It accepts local statements followed by an optional final expression,
without a book or paragraph wrapper. This is the admission foundation for the
[Anomalous shell proposal](../design/NIL-0018-matrix-shell-profile.md).

```sh
printf 'value amount is 40\namount plus 2\n' | nilcompiler --check-submission -
nilcompiler --check-submission session.mat
nilcompiler --check-submission-request request.nildata
```

Each command reads one bounded regular file or standard input through EOF and
writes one JSON report to standard output. It performs parsing, profile checks,
ordinary type checking and NIL generation. It does not execute the submission,
retain bindings, import libraries or write a compiled artifact. These commands
must be used alone; compilation/output switches cannot change their behavior.

## Results and completeness

The report schema is `nil.matrix.submission-check.v1`; the admission profile is
`matrix.pure.v1`. Reports include `input_kind`, `status`, `executed: false`,
`result_type`, `nil`, `instruction_count` and `diagnostics`. Only a complete,
admitted submission has a result type and generated NIL. The type is `void`
when there is no final expression. `nil` contains inspection text, not a runtime
authorization or a serialized session. There is no evaluated result field.

| Status | Exit code | Meaning |
| --- | --- | --- |
| `complete` | 0 | Parsing, type checking and both profile gates passed. |
| `incomplete` | 2 | Matrix syntax needs more input, such as an operand or closing block. |
| `invalid` | 1 | Input, parsing, schema, type or profile checks failed. |

Malformed command options produce a normal command-line error rather than a
submission report. NILData requests must be complete documents; malformed or
incomplete requests are invalid. A caller collecting interactive lines can
resubmit the accumulated Matrix text after an incomplete result. Checks do not
share parser or execution state.

Diagnostics carry stage, code, message, location, line, column and length. Matrix
parser diagnostics retain original source positions; later stages can provide
line-only positions. Structured expression errors identify request paths such
as `$.value` or `$.bindings[0].value`. Zero positions mean unavailable.

## Admitted computation

The initial profile admits immutable `value` bindings, supported scalar/text
expressions, tuple construction/destructuring, concrete arrays and indexing,
numeric casts, formatted text and conditionals. A single bare expression may
appear at the end. Use a value position or parentheses for a value-producing
conditional when its syntax would otherwise select a statement.

Calls (including library functions), imports, named declarations, annotations,
mutation, loops, collected queries, pointer/reference operations and assembly
are rejected. Both branches are checked, even if one is unreachable. The
generated NIL gate independently checks the entire function, type metadata,
stack/local initialization, fresh tuple construction and forward control flow.
It rejects any unknown or forbidden opcode.

This is a narrower profile than ordinary Matrix. Its exclusions do not change
the language accepted in normal books. The full-program
`shell-profile-preview.matrix` sample uses calls, queries and output, so it is
not an admitted pure submission.

Admission does not prove that evaluation will succeed: division by zero and
out-of-range indexing can pass checking. Before execution is added, a hosted
evaluator needs allocation/work budgets, cancellation, result projection and
explicit ownership rules for retained bindings. Live Anomalous commands also
need their separate invocation and authority checks.

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
References must resolve to an earlier binding; they cannot name host operations.

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

## Bounds and tests

Both inputs are limited to 65,536 bytes of valid UTF-8 with no embedded NUL.
Matrix submission scanning has a 4,096-token limit and parsing has a 64-level
nesting limit. Requests also use the shared NILData format limits and a 32-level
expression decoder limit. AST/profile checks allow at most 4,096 nodes and 64
levels, with additional limits for local names, collections and type shapes.
Generated NIL is capped at 4,096 instructions, 256 locals/stack slots and 1 MiB
of tracked text. These are admission limits, not execution quotas.

`matrix.submission-parser`, `matrix.submission-request`,
`nil.submission-profile` and `matrix.submission-contract` cover completeness,
strict decoding, forbidden AST/NIL operations, resource limits, diagnostics,
source/request equivalence and command isolation. They run without an executor.
