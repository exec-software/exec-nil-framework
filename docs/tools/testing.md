# Testing Ny and Matrix programs

`niltest` discovers tests through the compiler and runs them against the NIL
interpreter, native compilation, or both. Tests can live beside the functions
they exercise. Ny and Matrix share the same test metadata, assertion behavior,
and execution protocol, including in mixed-language import graphs.

Discovery checks the saved source graph without running application code or
global initializers. Each selected test runs in a separate process with fresh
globals. The application entrypoint is not called automatically; a test can call
it explicitly.

## Write tests beside the implementation

In Ny, use a named test block:

```ny
fn echo(input: i32) -> i32
    return input
end fn

@suite("arithmetic")
test "preserves the input"
    assert.equal(echo(7), 7, "echo must preserve its input")
    assert.true(yes)
end test
```

The equivalent Matrix source uses `begin test` and `expect`:

```matrix
book testing.example

echo receives input as number returns number begin paragraph
    give input
end paragraph

note suite "arithmetic"
preserves_the_input begin test
    expect (echo with 7) equals 7
    expect yes
end test
```

`@suite("name")` in Ny and `note suite "name"` in Matrix group tests in reports
and the editor. A suite takes one nonempty text argument. Without an explicit
suite, discovery uses the enclosing module name when one is available.

A module-level function can also be a test. It must take no arguments, return
`void`, and have no generic parameters:

```ny
@suite("arithmetic")
@test
fn positive() -> void
    assert.greater_than(2, 1)
end fn
```

```matrix
note suite "arithmetic"
note test
positive begin paragraph
    expect 2 is greater than 1
end paragraph
```

Ny also discovers deterministic `example` blocks and test blocks attached
immediately after a function declaration. An attached block is a separate test
body: it calls the function explicitly and does not receive that function's
parameters or local variables. An attached case beside a generic module
function is discovered once and can call concrete specializations. Tests attached
to methods are currently rejected; write a module-level case that constructs
the receiver explicitly. `@integration` labels a test as an integration
case; it uses the same process isolation and execution rules.

## Inline checks and production builds

A test block inside a function is a lexical check at that point in execution:

```ny
fn echo(input: i32) -> i32
    let observed = input
    test "local invariant"
        assert.equal(observed, input)
    end test
    return observed
end fn
```

```matrix
echo receives input as number returns number begin paragraph
    value observed is input
    local-invariant begin test
        expect observed equals input
    end test
    give observed
end paragraph
```

Inline blocks can access visible parameters and local variables and introduce
their own local scope. They run when a selected test calls the enclosing
function. They are not separately selectable tests and do not cause functions
to run during discovery. An inline block cannot return from its enclosing
function or break or continue an enclosing loop. Loops and nested functions
created inside the block retain their own control-flow boundaries.

Ordinary compilation removes test declarations, attached and inline test blocks,
and functions marked `@test` or `@if_test_mode` before type and ownership
checking. Matrix accepts the corresponding `note test` and `note if_test_mode`
annotations. Test-only code still needs valid syntax. `--test` and `--test-list`
include and check it; neither permits `--no-typecheck` or library emission.

Assertions outside a test-only region remain runtime checks in production.
Use test blocks for checks that should exist only in test builds.

## Assertions

Ny provides the following checked assertion forms. Each accepts an optional
final text message.

| Form | Supported operands |
| --- | --- |
| `assert.true(condition)`, `assert.false(condition)` | Boolean; `assert.yes` and `assert.no` are aliases. |
| `assert.equal(actual, expected)`, `assert.not_equal(actual, expected)` | Numeric scalars, two Boolean values, or two text values. |
| `assert.greater_than(actual, expected)`, `assert.greater_or_equal(actual, expected)` | Numeric scalars. |
| `assert.less_than(actual, expected)`, `assert.less_or_equal(actual, expected)` | Numeric scalars. |

Numeric scalars here are `u8`, `i32`, `i64`, `f32`, and `f64`. Floating equality
is exact; no tolerance is implied. Arrays, records, classes, and other aggregate
values do not yet have automatic deep-equality assertions. Compare the fields
or elements relevant to the test.

Matrix `expect` accepts a Boolean expression or a supported comparison:

```matrix
expect yes
expect not no
expect actual equals expected
expect "A" does not equal "B"
expect 2 is greater than 1
expect 2 is at least 2
expect 1.5 is less than 2
expect 2 is at most 2.0
```

Operands are evaluated once, in order. Ny's optional message is also evaluated
once, including when the assertion passes. Matrix `expect` does not yet have a
custom-message clause. A failed assertion stops that case
with its source file, line, comparison, actual value, expected value, and
optional message. The runner continues with other selected cases.

### Standard-library helpers

Import `nil.std.testing` with Ny's `import` or Matrix's `read`. It provides
`expect-true`, `expect-false`, `expect-number`, `expect-floating`, `expect-text`,
and `expect-boolean`. Matrix uses these spellings; Ny calls the corresponding
underscore names, such as `expect_number(actual, expected)`.

`require-some<T>(input, message)` and `require-ok<T, E>(input, message)` recover
successful `Option` and `Result` payloads. They return the payload for use in an
ordinary assignment and fail with the supplied text when the value is absent
or an error. They do not invent default values.

```matrix
book testing.recovery

read nil.std.testing
read nil.std.core.option
read nil.std.core.result

recover_values begin test
    value found as Option of number is Option.Some holding 42
    value actual is require-some of number with input being found, message being "expected a value"
    expect actual equals 42

    value outcome as Result of number, text is Result.Ok holding 7
    value answer is require-ok of number, text with input being outcome, message being "expected success"
    expect answer equals 7
end test
```

These helpers are ordinary library calls and remain active outside test blocks.
In Ny their exported names are `require_some` and `require_ok`.

## Discover and run from the command line

Install the SDK tools together or configure explicit executable paths:

```sh
niltest list src/main.matrix
niltest run src/main.matrix
niltest run src/main.matrix --target native
niltest run src/main.matrix --target all --jobs 4
niltest run src/main.matrix --filter arithmetic
niltest run src/main.matrix --test-id ID_FROM_LIST --test-id ANOTHER_ID
niltest run src/main.matrix --timeout 10 --format json
niltest run src/main.matrix --format nildata
```

Pass one or more `.ny`, `.matrix`, or `.mat` sources. Discovery includes their
source imports; it does not scan unrelated files in a directory. IDs are opaque
identifiers derived from source identity, location, owner, and name. Obtain them
from discovery and refresh after moving or renaming tests.

`--filter` matches a case-sensitive substring of the test ID, name, or suite.
Repeated `--test-id` arguments select exact IDs. When both options are present,
a case must satisfy both. Unknown IDs are errors. An empty discovery list is
valid, but `run` with no matching tests fails instead of reporting success.

Tool overrides are `--compiler PATH` and `--interpreter PATH`; otherwise tools
resolve beside `niltest`, then on `PATH`. Repeat `--sdk-path PATH` and `-I PATH`
to supply import roots. `--no-stdlib` disables implicit standard-library lookup.
Native execution requires an LLVM-enabled compiler and a working host linker.
Execution currently requires a POSIX host; Windows execution is not supported.

Each requested route compiles once into a private temporary directory. Selected
cases then run independently, with standard input closed and captured standard
output and standard error. `--jobs` allows 1–64 concurrent cases, defaulting to
at most four. A failing case does not cancel its siblings. Cancellation stops
new cases and terminates active process groups. Background descendants in those
process groups are cleaned up when a case finishes.

The default case deadline is 30 seconds. `--timeout` accepts 0.001–3600 seconds.
Discovery and each compilation have a 300-second limit. Capture is bounded to
16 MiB per child and 64 MiB across executed cases; exceeding a limit produces an
error result. This is process isolation, not an operating-system sandbox.

Human output is the default. `--format json` writes one complete report to
standard output, with case output stored in fields rather than mixed into the
protocol. Discovery uses `nil.tests.v1`; execution uses `nil.test.results.v1`.
Execution records include the test metadata, route, status, exit code, signal,
timeout/cancellation flags, duration in milliseconds, and captured output.
Statuses are `passed`, `failed`, `timeout`, `cancelled`, or `error`; infrastructure
errors also appear in `errors`. Results are delivered after the run completes.

NILData reports contain the same data and use the existing bounded codec:
1 MiB, 65,536 values, and 32 nesting levels. Reports exceeding that profile fail
serialization with an explanation; use JSON for larger captured output.

The runner exits `0` when every selected case passes, `1` for failing or timed-out
cases, and `2` for invalid input, selection, or infrastructure errors. Cancellation
returns `128 + signal`. A compiled test harness reserves exit `77` for completed
cases; an application-level `exit(0)` is an incomplete test and fails.

### Compiler interface

Tool authors can use the compiler protocol directly:

```sh
nilcompiler --test-list -I src src/main.matrix
nilcompiler --test --no-bundle --output tests.nil src/main.matrix
nilinterpreter --no-ai tests.nil -- ID_FROM_LIST
nilcompiler --test --native --output tests.native src/main.matrix
./tests.native ID_FROM_LIST
```

`--test-list` emits checked JSON metadata without creating an artifact or running
user code. A harness accepts exactly one discovered ID and returns `2` for a
missing, extra, or unknown selection. It initializes globals only after matching
the ID. Prefer `niltest` for ordinary use so deadlines, capture, result reporting,
and process cleanup are applied consistently.

## Run a project

[Project manifests](projects.md) already identify the source entrypoint, SDK,
and include paths. Use that configuration directly:

```sh
nilbuild test
nilbuild test --target all --jobs 4
nilbuild test --filter arithmetic --timeout 10 --format json
nilbuild test --project path/to/nil.project.nildata --test-id ID_FROM_LIST
```

`nilbuild test` delegates to `niltest run` with the resolved project entry and
import settings, using the project directory as the working directory. It does
not build project-reference executables, use the application build cache, or
publish over the application's VM/native outputs. Each invocation builds fresh
test artifacts. There is no separate manifest test section in this version.
`--tester PATH` overrides the runner; compiler/interpreter and SDK overrides
remain available.

## Visual Studio Code

In a trusted workspace, open a saved Ny/Matrix source or a file in a NIL project
and run **NIL Framework: Discover Tests in Current File or Project**. The Testing
view groups discovered cases by suite and links them to source locations. A
project uses its configured entrypoint and import graph; a standalone source is
its own discovery root. Save dirty Ny/Matrix buffers before discovery.

Run selected cases with the **NIL interpreter** profile, or choose **NIL native**
for native compilation. Running requests a save before invoking the runner.
Refresh discovery after moving or renaming tests. The extension reads compiler
metadata and runner results; it does not infer runnable tests from text patterns.

The extension uses the selected SDK and project import settings.
`ny.testTool.path` overrides `niltest`, and `ny.testing.timeoutSeconds` sets the
case deadline from 1–300 seconds, default 30. The editor additionally bounds each
operation and captures at most 32 MiB of runner output. Case output, failures,
and durations appear in the Testing view.

Property generators, shrinking, coverage collection, and test-debug profiles are
not shipped in this version. Unsupported generator annotations are diagnosed.
The [developer tooling plan](../developer-tooling.md) describes the broader
compiler and editor work.
