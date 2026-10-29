# NIL-0024: Tests alongside source

Status: implemented initial unit and integration test profile.

## Purpose

Tests belong to the same source graph as the behavior they exercise. Both
frontends lower test declarations and assertions to the shared AST. The compiler
checks their types, ownership and source identity; tools consume its discovery
manifest instead of recognizing tests with text searches.

## Source model

Ny named `test` and `example` blocks, Matrix `<name> begin test` blocks, and
zero-argument, nongeneric void functions annotated `@test` are runnable cases.
`@suite("name")` supplies a group; Matrix can spell these annotations `note test`
and `note suite "name"`. Existing Ny tests attached after a function remain
supported. Integration annotations describe cases without changing execution.

A named test block inside a function is an inline test region. It sees the
function's preceding locals and introduces a child lexical scope. It executes
when a discovered case calls that function in a test build. It does not invent
arguments or become an independent case. Ownership analysis includes the region;
returning from it or jumping to an enclosing loop is rejected. Loops declared
inside the region retain ordinary control flow.

Production compilation removes test declarations, annotated test functions and
inline regions before semantic checking and lowering. Test-only declarations
cannot provide symbols to production code. Syntax still has to parse. Editor
analysis retains the test code so errors remain visible while editing.

Matrix `expect expression` lowers to the same assertion node as Ny `assert`.
Direct comparisons retain both operands for diagnostics. Supported assertions
check Boolean conditions or scalar/text equality and numeric ordering. Arguments
are evaluated once, in source order, including an optional Ny message. A failed
check reports its source, assertion and actual/expected values. Ordinary
assertions outside test-only regions are runtime checks. Property generators
and unsupported assertion methods produce explicit diagnostics.

## Discovery and execution

`nilcompiler --test-list` checks the complete input/import graph and emits
`nil.tests.v1` JSON. It never executes user code or initializers. Each case has an
opaque deterministic ID, name, suite, kind, function and source location. IDs
identify a particular checked source layout, not a permanent external API.

`nilcompiler --test` adds a synthetic entrypoint selected by one exact case ID.
It calls the case directly, avoiding an unimplemented native function-pointer
ABI. The original application entrypoint remains callable under a private
name. Global initialization runs once after a valid case is selected. The
harness exits with private completion status 77; an early user `exit(0)` does
not count as completion. This status is an execution convention, not a security
boundary against a deliberately forged test.

`niltest` compiles once for each requested VM/native route, then runs each
selected case in its own process. A fault, signal or timeout is a case result;
ordinary failures do not stop later cases. Process groups provide cancellation
and descendant cleanup. Captured stdout/stderr are data inside a result record,
never a status protocol. JSON and NILData reports identify the route explicitly.
Current process execution requires a POSIX host.

`nilbuild test` delegates with the project's entrypoint and import/SDK paths.
VS Code discovers from saved sources in a trusted workspace and supplies
interpreter/native Test Explorer run profiles. It validates the structured
reports and rejects stale/unknown identities instead of guessing a match.

## Library and limits

The Matrix-authored `nil.std.testing` provides scalar checks and generic
`require-some`/`require-ok` value extraction. An unsuccessful extraction faults
with the supplied explanation; it does not fabricate a value. The module's own
tests live beside its implementation.

The initial profile does not include property generation, coverage, fixtures
with shared mutable process state, automatic test-input synthesis, or a Test
Explorer debugging profile. Source maps for preprocessor transformations remain
tracked separately. Imported source tests participate in discovery; callers can
filter suites or exact IDs. Binary libraries do not expose runnable test source.

## Evidence

- `tests/tools/niltest_compiler_contract.py`: read-only discovery, harness
  selection, initialization and artifact preservation on VM/native routes.
- `tests/tools/niltest_source_contract.py`: both source languages, production
  exclusion, lexical scope, assertions, ownership and standard-library helpers.
- `tests/tools/niltest_process_contract.py`: isolated results, cancellation,
  timeouts, descendant cleanup, selection and malformed discovery.
- Editor model and real extension-host tests: report validation, source identity
  and compiler-backed discovery.

See the [testing guide](../tools/testing.md) for runnable examples and limits.
