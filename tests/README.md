# Tests and fixtures

The root CMake project registers layered checks for the frontend, NIL formats,
interpreter, bundles, and optional LLVM routes. The basic smoke pair compiles
`tests/smoke/hello.ny` to a separate NIL file and runs it with
`nilinterpreter`. Additional tests cover binary NIL magic and loading, function
boundaries, program arguments, exit-code propagation, runtime errors, and
rejection of malformed source and NIL. Focused route contracts also keep
function/default/builtin arity, entry-point shape, generic method parameters,
intrinsic wrapper parameters, and integer overflow behavior from regressing.

The `ny.repository-policy` test also enforces the canonical public runtime name
and prevents the retired source-conversion feature or embedded C++ fragments
from returning unnoticed.

Run them after configuring and building:

```bash
ctest --test-dir build/cmake --output-on-failure
```

`tests/routes/ny_route_matrix.py` validates exact stdout, stderr, exit status,
and generated artifacts across textual NIL, binary NIL, both bundled forms,
LLVM JIT, and LLVM AOT. Its [README](routes/README.md) documents fixtures,
expected-failure handling, and standalone use.

`tests/tools/native_toolchain_pipeline.py` keeps the standalone native stages
honest: it compiles both textual and binary NIL, assembles each into a real host
object, links it with `nilruntime`, and executes the result. It also covers
hostile path characters, private output modes, malformed NIL and object input,
NIL-library rejection, and rollback when a command fails. The component-level
`nillinker.host-object` test independently proves multi-object linking, SDK
runtime discovery (including real standalone build/install layouts), Unicode
object/runtime/output paths, no-follow immutable input capture,
object-count/byte budgets, hostile special-file rejection, bounded driver
timeout, output preservation, and private-stage cleanup.
`nillinker.process-deadline` separately
proves exact exit propagation, bounded cleanup, and rejection of a driver that
exits while descendants remain in its POSIX process group or Windows Job
Object.

CTest also runs the curated [application suite](../examples/applications/README.md).
LLVM-enabled builds exercise all six routes, including the manifest's explicit
native XFAILs; LLVM-disabled builds exercise both interpreter formats and both
bundled formats. The standalone driver remains useful for selecting routes and
retaining reports outside a CMake build tree.

`tests/language` contains focused parser and compiler regression inputs. The
standalone parser test confirms inline-assembly syntax remains represented in
the AST. `inline-assembly.contract` validates the bounded x86_64 register profile;
`ny.inline-assembly` proves native JIT/AOT and textual/binary assembler/linker
execution plus explicit rejection by both interpreter and bundle formats.
The targetless legacy assembly form remains a compiler-rejection fixture.
`ny.tools.native-nil-validation` rejects malformed native stack/control-flow
artifacts while preserving old output; the string-escape parity fixture checks
all six source routes and both serialized native toolchain routes.
`nil.loader` also verifies interpreted call-frame operand isolation through
both NIL encodings, including builtin/interop argument preflight, aggregate
mutation rejection, optional allocation operands, recursion, and repeated
embedding calls. These checks do not claim full return-stack ABI parity.
The lexical-storage and lexical-aggregate-storage fixtures verify shadowing,
initializers, generic/recursive functions, array loops, struct writeback and
mixed-type enum bindings across all six source routes. The schema-free fixture
also passes through textual/binary native object assembly and linking.
`ny.variable-scope` preserves duplicate, visibility, mutability and unsupported
capture rejection, with interpreted callback/reference/tuple-loop controls.
Exact compiler-rejection routes also
cover unavailable fault flow, postfix propagation, compiler intrinsics,
structured match payloads, and formerly invalid-NIL AST fallbacks. The
registered subset is referenced from the root `CMakeLists.txt`; other files
remain probes. A checked-in source is not evidence of support unless a route
test asserts its behavior.

The `ny.routes.threading-sleep` contract executes the active timed-suspension
API through textual, binary, and bundled NIL, plus LLVM JIT and AOT.
`ny.routes.reject-unbacked-concurrency` also verifies that
the quarantined thread, atomic, channel, synchronization, wait-handle, and task
names are absent from the active SDK surface.

Standard-library test sources live under `nilstd/tests`; their current
failures and promotion criteria are documented in `nilstd/README.md`.
Experimental UI tests live under `nilaix/tests` and have separate native and
platform requirements.
