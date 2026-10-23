# Aperture memory profiling

Aperture's first complete workflow records a program, reports where runtime
memory accumulates, and provides source locations for investigation. The CLI and
runtime share a versioned report format so editor features and automated tools
can consume the same evidence.

## Recording flow

1. The CLI validates the target and exclusively reserves an optional output
   destination before execution.
2. Source inputs are compiled with debug metadata to temporary NIL. Native source
   inputs then pass through `nilassembler` and `nillinker` using that metadata.
3. Aperture starts the interpreter or native target directly, with an argument
   vector and no shell. `NIL_MEMORY_PROFILE` names a fresh file in a private
   temporary directory. Compilation tools do not receive that setting.
4. The runtime reserves its report path exclusively, records allocations and
   releases, and finalizes the report before tearing down its managed state.
5. Aperture validates the report before displaying or publishing it. Failed or
   incomplete runs remain explicitly identified; a successful target without a
   valid completed profile is a profiling failure.

The child inherits standard input/output/error and the caller's working
directory. Target output stays on standard output; recording summaries go to
standard error. Saved-report inspection uses standard output and does not run a
program. Target exit codes are propagated, including signal-derived statuses.

## Runtime contract

`NIL_MEMORY_PROFILE` activates an opt-in recording session. The shared collector
is inert when the variable is absent. Runtime hooks identify allocation sites,
record allocation and resize deltas, and record logical release and collection
events. The native compiler emits source-site updates at allocating instructions;
the interpreter uses its current instruction and debug metadata.

A session writes one `aperture.memory.v1` JSON object containing:

| Field | Meaning |
|---|---|
| `schema` | Exactly `aperture.memory.v1` |
| `route` | `vm` or `native` |
| `measurement` | `runtime_logical_bytes` |
| `completed`, `status` | Whether recording finalized, and how execution ended |
| `totals` | Allocated/freed/live/peak bytes and objects; resize count |
| `collections` | Collection count, elapsed nanoseconds, reclaimed bytes and objects |
| `sites` | The same counters grouped by file, function, line, and allocation kind |
| `scope` | Accounting boundaries and runtime-specific exclusions |
| `errors` | Instrumentation/reporting failures |
| `excluded_sessions` | Runtime sessions outside the recorded session |
| `site_limit_reached` | Whether source-site detail reached its recording limit |

Byte counters use delta accounting: growth adds allocated bytes, shrinkage adds
freed bytes, and resizing does not create or free an object. Therefore
`allocated - freed = live`, with `live <= peak <= allocated`. Peak values are
maxima over the session, not sums of per-site peaks. Unknown source metadata uses
an empty file and line zero; it must not be replaced with guessed source lines.

Reports aggregate sites rather than retaining all individual events. Native and
VM storage models differ, so byte-for-byte parity is not a contract. Both routes
must preserve program behavior and identify the relevant source allocation.
The report is finalized before ordinary shutdown cleanup, retaining useful live
storage evidence instead of automatically reporting zero.

## Limits and failure behavior

Live allocations are not classified as leaks. No root-retention graph, allocation
lifetime, full call stack, allocation-rate timeline, CPU sample, or debugger heap
snapshot is produced yet. Source locations can be unavailable in artifacts built
without debug metadata. Allocations outside the NIL runtime's tracked storage
are outside the report's scope. In native unmanaged mode, raw allocations,
records, variants, and string concatenation use libc directly and are excluded;
arrays and other storage allocated through the NIL runtime remain tracked.

The CLI rejects malformed JSON, unknown schema versions, missing fields,
inconsistent counters, oversized reports, and unsupported status values. It
never interprets report strings as commands. Existing output files and symlink
referents are not overwritten. Runtime output failures are diagnostics and
nonzero results, not apparently successful empty profiles. Abrupt process death
may leave an unfinished temporary report and cannot promise a final snapshot.

## Next integrations

VS Code can launch recording and read the same saved JSON for source navigation.
Future debugger support should reuse the source identity and allocation hooks to
capture snapshots while paused. Retention analysis, snapshot comparison,
allocation timelines, and CPU call trees can extend the suite without claiming
those features in the current memory report.
