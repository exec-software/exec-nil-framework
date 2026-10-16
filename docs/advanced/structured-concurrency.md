# Structured scalar concurrency

Ny provides real, bounded fork/join execution through `parallel_map`:

```ny
fn transform(pixel: i64) -> i64
    return (pixel ^ 255) + 3
end fn

fn main() -> i32
    let pixels: array<i64> = [1 as i64, 2 as i64, 3 as i64]
    let encoded = parallel_map(transform, pixels)
    println(encoded[0])
    return 0
end fn
```

Each item is copied to an isolated worker. Results occupy the same indices as
their inputs. The input array is not mutated, and the caller receives a newly
allocated result array only after every submitted item and every worker thread
has finished. There are no detached tasks or task handles to forget to join.

## Supported contract

The first argument names a statically resolved, nongeneric Ny source function
with exactly one parameter and one result. Parameter and result types are each
one of `i32`, `i64`, `u8`, `bool`, or `f64`; they need not be the same type. The
second argument must be `array<parameter_type>`, and the result is
`array<result_type>`. Function-valued locals, closures, method receivers,
captures, and implicit input-array conversions are not accepted.

Workers may use scalar arithmetic, bitwise operations, comparisons, scalar
casts, local variables, branches, loops, and direct calls to other isolated
scalar helpers. Helpers may also return `void`. The complete transitive call
graph is checked, including instructions in branches that happen not to run.

Workers cannot access globals, pointers, owned allocations, loans, aggregate
handles, arrays, text, class methods, nested `parallel_map`, inline assembly,
FFI, console or file I/O, process operations, clocks, sleeps, or Ny fault
payloads. Intrinsic/external declarations are not accepted as worker helpers.
These restrictions are checked on NIL as well as source; hand-written NIL and
`--no-typecheck` cannot grant a worker additional effects.

This is a scalar data-parallel facility, useful for independent numeric and
packed-pixel transforms. It is **not** a general async runtime, shared-memory
thread API, channels system, or a claim that the experimental concurrency
standard library is supported. Batching structured objects, cancellation,
shared synchronization primitives, and arbitrary task scheduling require
separate contracts.

## Scheduling, bounds, and failures

Nonempty maps run on actual host threads, never a sequential emulation on the
caller thread. The pool size is the minimum of the item count, eight, and the
reported host hardware concurrency (with a floor of two for a multi-item map).
An empty map returns an empty array without starting a worker. One map accepts
at most 1,048,576 inputs; larger inputs fail before workers are started.

Scheduling order is unspecified. Result order is deterministic. If a worker
encounters a checked arithmetic/runtime error, other submitted inputs are
still processed and all worker threads are joined. The lowest failing input
index is then reported, for example:

```text
PARALLEL_MAP: worker at input 0 failed: ...
```

This is a runtime error, not a catchable Ny fault payload. No partial result
array is returned. A host failure while creating threads also joins every
thread already started before reporting failure. User code that does not
terminate is not forcibly cancelled; the map cannot finish until its workers
finish. An instruction-stepping debugger observes the map as a blocking
instruction and can pause again when it completes.

## Execution and artifact contract

The operation is preserved in textual and binary executable NIL:

```text
PARALLEL_MAP worker_name i64 i32
```

The three operands are the exact worker symbol, input scalar type, and output
scalar type. It consumes one typed input-array handle and produces one typed
result-array handle. Opcode names use the extensible NIL instruction encoding;
older consumers that do not implement this opcode reject it. No platform
thread pointer or function address is serialized into NIL.

The interpreter copies scalar inputs and runs independent VM instances. Worker
VMs receive no parent globals, heap, arguments, file handles, debugger callbacks,
or redirected host streams. Immutable program/schema metadata may be shared.

Native JIT, direct native compilation, and the `nilassembler` → `nillinker` route
use the same versioned runtime entry point:

```cpp
void *nyrt_parallel_map_v1(
    const void *inputs, uint32_t input_kind, uint32_t output_kind,
    uint64_t (*worker)(uint64_t));
```

The compiler generates the callback; Ny source cannot supply a foreign callback.
Scalar words, including `f64` bit patterns, cross this private boundary by value.
Workers receive independent thread-local runtime contexts. Input handles never
cross that boundary, and the result array belongs to the parent context.

Native worker call graphs have private implementations that turn generated
runtime-error edges into thread-local error records and propagate the failure
through helper calls. Process failure is reported only after joining workers.
Calling the same source helper outside a map retains its ordinary behavior.

The same shared scheduling primitive is tested using a bounded atomic barrier
that requires overlapping host workers, rather than inferring concurrency from
sleep duration. Tests also cover scalar routes, independent contexts, ordered
results, preserved inputs, rejected effects, and joined failure paths.
