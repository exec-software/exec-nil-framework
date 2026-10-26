# Aperture

Aperture is the NIL framework's runtime diagnostics tool. Its first implemented
capability records memory activity from Ny and Matrix programs running in the
interpreter or as native executables, then connects allocation totals to source
locations.

## Record a memory profile

Aperture ships with the SDK and uses the compiler, interpreter, assembler, and
linker installed beside it.

```sh
# Compile and run source in the VM, saving a new report.
sdk/aperture memory --output vm-memory.json example.matrix

# Compile the same source with source metadata, then assemble and link natively.
sdk/aperture memory --native --output native-memory.json example.matrix

# Profile an existing NIL artifact or an instrumented native executable.
sdk/aperture memory --output memory.json example.nil
sdk/aperture memory --output native.json ./example

# Inspect a saved report without executing the program again.
sdk/aperture memory --report memory.json
sdk/aperture memory --report memory.json --json
```

Recording preserves the program's standard input, output, error stream, working
directory, and exit status. The profile summary goes to standard error. `--json`
prints the full report there while recording; use `--output` or saved-report
inspection for a separate machine-readable stream. Program arguments follow
`--`, for example `aperture memory ./example -- "input file.txt"`.

`--output` creates a new file and refuses to overwrite existing files, including
symlinks. A missing, malformed, or incomplete runtime profile makes recording
fail; a nonzero program exit remains the command's exit status. A valid report
can still be saved after a program failure. Abrupt termination may prevent the
runtime from finishing its report.

For source compilation, `--sdk-path <directory>` selects the standard-library
search root. This is a single-file workflow; project builds and dependency
configuration remain the responsibility of `nilbuild`. Programs compiled
separately need `--debug-info` in their NIL to retain source paths and line
numbers. Native source recording goes through that annotated NIL before native
assembly and linking. Older native programs must be rebuilt against the updated
runtime to produce profiles.

`--memory-mode safe|unsafe` selects the interpreter memory mode, or the mode used
to compile source natively. Without it, each tool keeps its normal default. The
mode of an existing native executable cannot be changed by the profiler.

## What the report measures

Reports use the versioned `aperture.memory.v1` JSON schema. They contain:

- Cumulative allocations and frees, current live storage, peak live storage, and
  resize counts, in objects and runtime logical bytes.
- Allocation sites with source file, function, line, and allocation kind.
- Garbage-collection count, elapsed collection time, and reclaimed storage.
- Completion state, instrumentation errors, and scope limitations.

Logical bytes describe storage tracked by each execution runtime. They are not
process RSS, allocator overhead, or a guarantee of identical byte counts between
VM and native execution. Live totals are captured before runtime teardown. An
object remaining live at that point is **not a proven memory leak**.

Source attribution identifies the allocating instruction; it does not yet retain
full call stacks or ownership/retention paths. Reports aggregate allocation sites,
not individual allocation events. Reports include absolute source paths and
function names, so review them before sharing outside your project.

## Current scope

Recording is supported on POSIX hosts. Saved JSON reports can be inspected
without executing their targets. The current profiler covers the NIL runtime
allocations described in each report's `scope`. VM coverage currently includes
typed arrays, raw buffers, and iterator payloads; VM text and record storage,
container capacity, and `Value` overhead are excluded. Native coverage includes
runtime-managed allocations, array descriptors and capacity, and runtime buffer
storage. Native unmanaged instructions that allocate directly through libc are
excluded, including raw allocations, records, variants, and string concatenation.
External library allocations, OS resources, and unrelated process memory are
outside that accounting.

Heap snapshots and comparison, retained-object graphs, allocation rates and
lifetimes, invalid-access diagnostics, CPU profiling, debugger capture, and an
interactive analysis interface are planned. The `explore`, `trace`, and `agent`
commands currently return an explicit unsupported-command error.

Build with the framework or generate a complete SDK using
`./scripts/build-sdk.sh`. See [design notes](docs/DESIGN.md) for the runtime
contract and integration boundaries.
